#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/wait.h>

#include "pci.h"
#include "registers.h"
#include "irq.h"
#include "cdev.h"

struct global_drv_data drv_data;


const struct pci_device_id id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {0}
};


static int probe(struct pci_dev *device, const struct pci_device_id *ent) {
    int err;
    void __iomem * const *iomap;
    struct device_data *data;

    pr_info("[FPGA] probe start\n");

    data = devm_kzalloc(&device->dev, sizeof(*data), GFP_KERNEL);
    if (!data) {
        pr_err("[FPGA] devm_kzalloc failed\n");
        return -ENOMEM;
    }
    pci_set_drvdata(device, data);

    /*
     * Всё, к чему обращается hardirq, должно быть инициализировано до
     * fpga_init_irq(): request_irq() и включение прерываний на карте
     * происходят именно там.
     */
    spin_lock_init(&data->event_lock);
    spin_lock_init(&data->reg_lock);
    mutex_init(&data->open_lock);
    init_waitqueue_head(&data->wq);


    err = pcim_enable_device(device);
    if (err) {
        pr_err("[FPGA] pcim_enable_device failed: %d\n", err);
        return err;
    }
    pr_info("[FPGA] device enabled\n");


    pci_set_master(device);
    pr_info("[FPGA] Bus master flag set\n");

    err = pcim_iomap_regions(device, BIT(BAR_CFG_IDX) | BIT(BAR_AXI_LITE_IDX), DRIVER_NAME);
    if (err) {
        pr_err("[FPGA] pcim_iomap_regions failed: %d\n", err);
        return err;
    }

    iomap = pcim_iomap_table(device);
    if (!iomap[BAR_CFG_IDX] || !iomap[BAR_AXI_LITE_IDX]) {
        pr_err("[FPGA] pcim_iomap_table returned NULL\n");
        return -ENOMEM;
    }
    data->bar[BAR_CFG_IDX] = iomap[BAR_CFG_IDX];
    data->bar[BAR_AXI_LITE_IDX] = iomap[BAR_AXI_LITE_IDX];
    pr_info("[FPGA] BARs valid\n");


    err = fpga_init_irq(device);
    if (err){
        return err;
    }

    err = fpga_init_chrdev(device);
    if (err){
        return err;
    }

    return 0;
}

static void remove(struct pci_dev* device){
    struct device_data* data = dev_get_drvdata(&device->dev);

    pr_info("[FPGA] driver remove function\n");

    /* Сначала убираем cdev, затем IRQ, векторы и bus-master. */
    fpga_free_chrdev(device);
    pr_info("[FPGA] chrdev free\n");

    devm_free_irq(&device->dev, data->irq_number, data);
    pci_free_irq_vectors(device);
    pr_info("[FPGA] IRQ vectors free\n");

    pci_clear_master(device);
    pr_info("[FPGA] Master flag clear\n");
}


struct pci_driver driver = {
    .name = DRIVER_NAME,
    .dev_groups = fpga_groups,
    .probe = probe,
    .remove = remove,
    .id_table = id_table
};

static int __init fpga_init(void){
    pr_info("[FPGA] init function called\n");
    drv_data.device_class  = class_create(DEVICE_CLASS_NAME);

    if (IS_ERR(drv_data.device_class)){
        pr_err("[FPGA] chardev class create fail\n");
        return PTR_ERR(drv_data.device_class);
    }
    pr_info("[FPGA] device class successfully allocated\n");
    return pci_register_driver(&driver);
}

static void __exit fpga_exit(void){
    pr_info("[FPGA] exit function called\n");
    pci_unregister_driver(&driver);
    class_destroy(drv_data.device_class);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");
