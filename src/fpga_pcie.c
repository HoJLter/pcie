#include <linux/pci.h>
#include <linux/module.h>
#include "registers.h"

#define DRIVER_NAME "Kintex-7 Driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define CNT_ALLOCATED_BYTES 8192

#define VEC_CNT 2


struct drv_data {
    void __iomem* bar0;
    struct pci_dev* device; 
};


static irqreturn_t turn_leds_on(int irq, void* dev){
    printk("[FPGA] IRQ DETECTED: TURN_ON_LEDS");
    struct drv_data* data = dev;
    writel(1, (data -> bar0) + LEDS_REG);
    return IRQ_HANDLED;
}

static irqreturn_t turn_leds_off(int irq, void* dev){
    printk("[FPGA] IRQ DETECTED: TURN_OFF_LEDS");
    struct drv_data* data = dev;
    writel(0, (data -> bar0) + LEDS_REG);
    return IRQ_HANDLED;
}

static int fpga_probe(struct pci_dev* device, const struct pci_device_id *ent){
    printk("BAR0 size = %llu\n", (unsigned long long)pci_resource_len(device, 0));

    struct drv_data* data = kzalloc(sizeof(struct drv_data), GFP_KERNEL);
    if (!data){
        return -ENOMEM;
    }
    data -> device = device;
    pci_set_drvdata(device, data);

    int err = pci_enable_device(device);
    if (err){
        printk("[FPGA] ERROR WHILE ENABLING DEVICE OCCURED: %d", err);
        return err;
    }
    err = pci_request_region(device, 0, DRIVER_NAME);
    if (err){
        printk("[FPGA] ERROR WHILE REQUESTING MEM REGION OCCURED: %d", err);
        pci_disable_device(device);
        return err;
    }

    data->bar0 = pci_iomap(device, 0, CNT_ALLOCATED_BYTES);
    if(!(data -> bar0)) {
        return -1;
    }

    int alloc_vec_cnt = pci_alloc_irq_vectors(device, VEC_CNT, VEC_CNT, PCI_IRQ_MSI);
    if (alloc_vec_cnt != VEC_CNT){
        printk("[FPGA] ERROR WHILE ALLOCATING IRQ VECTORS");
        return -1;
    }

    int turn_on_vec = pci_irq_vector(device, 0);
    int turn_off_vec = pci_irq_vector(device, 1);
    printk("[FPGA] IRQ VECTORS ALLOCATED: TURN ON - %d; TUNR OFF - %d", turn_on_vec, turn_off_vec);

    err = request_irq(turn_on_vec, turn_leds_on, 0,  "FPGA leds on", data);
    if (err){
        return err;
    }
    err = request_irq(turn_off_vec, turn_leds_off, 0, "FPGA leds off", data);
    if (err){
        return err;
    }

    return 0;
}

static void fpga_remove(struct pci_dev* device){
    printk("[FPGA] REMOVE FUNC CALLED");

    struct drv_data* data = pci_get_drvdata(device);

    free_irq(pci_irq_vector(device, 0), data);
    free_irq(pci_irq_vector(device, 1), data);

    pci_free_irq_vectors(device);

    pci_iounmap(device, data->bar0);
    pci_release_region(device, 0);
    pci_disable_device(device);

    kfree(data);
    pci_set_drvdata(device, NULL);
}

static struct pci_device_id fpga_pci_id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {0}
};

MODULE_DEVICE_TABLE(pci, fpga_pci_id_table);

struct pci_driver fpga_pci_driver = {
    .name = DRIVER_NAME,
    .id_table = fpga_pci_id_table,
    .probe = fpga_probe,
    .remove = fpga_remove 
};

static int __init fpga_init(void){
    printk("[FPGA] INIT FUNCTION");
    return pci_register_driver(&fpga_pci_driver);
}

static void __exit fpga_exit(void){
    printk("[FPGA] EXIT FUNCTION");

    pci_unregister_driver(&fpga_pci_driver);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");
