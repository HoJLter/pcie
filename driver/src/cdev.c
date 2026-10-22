#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include "registers.h"
#include "pci.h"
#include "cdev.h"


#define DEV_COUNT 1
#define BASEMINOR 0
#define DEV_NAME "fpga0"
#define SUBDEV_NAME "fpga_cdev_sub"

extern struct global_drv_data drv_data;

/* Функция, вызываемая при системном вызове open. Устройство — один потребитель. */
static int fpga_open(struct inode* inode, struct file* f){
    struct device_data* data = container_of(inode->i_cdev, struct device_data, char_dev);
    unsigned long flags;

    if (mutex_lock_interruptible(&data->open_lock))
        return -ERESTARTSYS;

    if (data->opened) {
        mutex_unlock(&data->open_lock);
        return -EBUSY;
    }

    /*
     * Отбрасываем прерывания, пришедшие до open(). Первый read() тогда
     * сообщает о свежем прерывании, а не о произвольно старом.
     */
    spin_lock_irqsave(&data->event_lock, flags);
    data->consumed_seq = data->irq_seq;
    spin_unlock_irqrestore(&data->event_lock, flags);

    data->opened = true;
    f->private_data = data;
    mutex_unlock(&data->open_lock);

    pr_debug("[FPGA] open\n");
    return 0;
}

/* Функция, вызываемая при close. Снимает запрет на повторный open. */
static int fpga_release(struct inode* inode, struct file* f){
    struct device_data* data = f->private_data;

    (void)inode;

    mutex_lock(&data->open_lock);
    data->opened = false;
    mutex_unlock(&data->open_lock);

    return 0;
}

/* Функция, вызываемая при системном вызове read. Ждёт событие и отдаёт его ABI. */
static ssize_t fpga_read(struct file* f, char __user* user_buffer, size_t size, loff_t* offset){
    struct device_data* data = f->private_data;
    struct fpga_irq_event ev;
    unsigned long flags;
    bool pending;
    int ret;

    (void)offset;

    if (size < sizeof(ev))
        return -EINVAL;          /* неполный или нулевой ABI-рекорд */

    for (;;) {
        spin_lock_irqsave(&data->event_lock, flags);
        pending = (data->irq_seq != data->consumed_seq);
        if (pending) {
            ev = data->event;
            ev.abi_version = FPGA_EVENT_ABI_VERSION;
            ev.size = (__u32)sizeof(ev);
            /*
             * missed считается в момент read(): consumed_seq ещё указывает
             * на последнее выданное событие, поэтому разница показывает,
             * сколько прерываний было перезаписано с тех пор.
             */
            ev.missed = ev.seq - data->consumed_seq - 1u;
            ev.read_ts_ns = ktime_get_ns();
            if (ev.read_ts_ns >= ev.irq_ts_ns)
                ev.latency_ns = ev.read_ts_ns - ev.irq_ts_ns;
            else
                ev.latency_ns = 0;   /* защита от рассинхронизации часов между CPU */
            data->consumed_seq = ev.seq;
        }
        spin_unlock_irqrestore(&data->event_lock, flags);

        if (pending)
            break;

        if (f->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(data->wq,
                    READ_ONCE(data->irq_seq) != READ_ONCE(data->consumed_seq));
        if (ret)
            return ret;          /* -ERESTARTSYS -> EINTR в userspace */
    }

    if (copy_to_user(user_buffer, &ev, sizeof(ev)))
        return -EFAULT;

    return sizeof(ev);
}

/* Структура, определяющая файловые операции над файловым дескриптором */
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = fpga_read,
    .open = fpga_open,
    .release = fpga_release
};

/* Функция, отвечающая за инициализацию character device */
int fpga_init_chrdev(struct pci_dev* device){
    int err;
    struct device* my_device;

    struct device_data* data = dev_get_drvdata(&device->dev);
    struct cdev* char_dev = &data->char_dev;

    BUILD_BUG_ON(sizeof(struct fpga_irq_event) != 40);

    /*
     * wq и блокировки инициализируются в probe() до fpga_init_irq(),
     * потому что прерывание может прийти сразу после request_irq().
     */
    err = alloc_chrdev_region(&data->dev_id, BASEMINOR, DEV_COUNT, DEV_NAME);
    if (err){
        pr_err("[FPGA] allocating chardev region fail\n");
        return err;
    }
    pr_info("[FPGA] allocating chardev region success\n");

    cdev_init(char_dev, &fops);
    err = cdev_add(char_dev, data->dev_id, DEV_COUNT);
    if (err){
        unregister_chrdev_region(data->dev_id, DEV_COUNT);
        pr_err("[FPGA] character device add fail\n");
        return err;
    }
    pr_info("[FPGA] character device add success\n");
    if(!drv_data.device_class){
        pr_err("[FPGA] device class is NULL\n");
        cdev_del(char_dev);
        unregister_chrdev_region(data->dev_id, DEV_COUNT);
        return -EINVAL;
    }
    my_device = device_create(drv_data.device_class, &device->dev,
                              data->dev_id, data, SUBDEV_NAME);
    if (IS_ERR(my_device)) {
        err = PTR_ERR(my_device);
        pr_err("[FPGA] device_create failed: %d\n", err);
        cdev_del(char_dev);
        unregister_chrdev_region(data->dev_id, DEV_COUNT);
        return err;
    }

    return 0;
}

/* Освобождение ресурсов character device */
void fpga_free_chrdev(struct pci_dev* device){
    struct device_data* data = dev_get_drvdata(&device->dev);
    struct cdev* char_dev = &data->char_dev;
    dev_t dev_id = data->dev_id;

    pr_info("[FPGA] free chrdev func\n");

    device_destroy(drv_data.device_class, dev_id);
    cdev_del(char_dev);
    unregister_chrdev_region(dev_id, DEV_COUNT);
}
