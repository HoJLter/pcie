#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include "registers.h"
#include "pci.h"


#define DEV_COUNT 1
#define BASEMINOR 0
#define DEV_NAME "fpga"

static dev_t dev_id;


static int fpga_open(struct inode* inode, struct file* f){
    pr_info("[FPGA] Open function call");
    f->private_data = container_of(inode->i_cdev, struct drv_data, char_dev);
    return 0;
}

static ssize_t fpga_read(struct file* f, char __user* user_buffer, size_t size, loff_t* offset){
    pr_info("[FPGA] Read function call. (Waiting for the interrupts)");
    int err;
    
    struct drv_data* data = f->private_data; 
    err = wait_event_interruptible(data->wq, data->is_irq);
    if (err){
        pr_info("[FPGA] wait event interruptible fail");
        return err;
    }

    data->is_irq = false;

    u8 value = 1;
    if (copy_to_user(user_buffer, &value, sizeof(value))){
        return -EFAULT;
    }
    
    return 1;
}

struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = fpga_read,
    .open = fpga_open
};


int fpga_init_chrdev(struct pci_dev* device){
    int err;

    struct drv_data* data = dev_get_drvdata(&device->dev);
    struct cdev* char_dev = &data->char_dev; 
    init_waitqueue_head(&data->wq);
    
    err = alloc_chrdev_region(&dev_id, BASEMINOR, DEV_COUNT, DEV_NAME);
    if (err){
        pr_err("[FPGA] allocating chardev region fail");
        return err;
    }
    pr_info("[FPGA] allocating chardev region success");
    

    cdev_init(char_dev, &fops);
    err = cdev_add(char_dev, dev_id, DEV_COUNT);
    if (err){
        unregister_chrdev_region(dev_id, DEV_COUNT);
        pr_err("[FPGA] character device add fail");
        return err;
    }
    pr_info("[FPGA] character device add success");

    struct class* fpga_class = class_create("fpga");
    struct device* my_device = device_create(fpga_class, &data->pdev->dev, dev_id, data, "fpga");
    if (IS_ERR(my_device)) {
        int err = PTR_ERR(my_device);
        pr_err("device_create failed: %d\n", err);
        return err;
    }

    return 0;
}

void fpga_free_chrdev(struct pci_dev* device){
    pr_info("[FPGA] free chrdev func");
    struct drv_data* data = dev_get_drvdata(&device->dev);
    struct cdev* char_dev = &data->char_dev;

    cdev_del(char_dev);
    unregister_chrdev_region(dev_id, DEV_COUNT);
}




