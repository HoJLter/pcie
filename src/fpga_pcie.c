#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include "registers.h"

#define DRIVER_NAME "Kintex-7 Driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define CNT_ALLOCATED_BYTES 0

#define VEC_CNT 1


struct drv_data {
    void __iomem* bar0;
    struct pci_dev* device; 
};

static irqreturn_t turn_leds_handler(int irq, void* dev){
    printk("[FPGA] IRQ DETECTED: TURN_LEDS");
    struct drv_data* data = dev;
    u32 is_turned_on = readl((data -> bar0) + LEDS_REG);
    printk("[FPGA] DATA READED: %d", is_turned_on);

    if (is_turned_on) writel(0, (data -> bar0) + LEDS_REG);
    else writel(1, (data -> bar0) + LEDS_REG);

    return IRQ_HANDLED;
}

static int fpga_probe(struct pci_dev* device, const struct pci_device_id *ent){
    printk("[FPGA] BAR0 size = %llu\n", (unsigned long long)pci_resource_len(device, 0));

    struct drv_data* data = kzalloc(sizeof(struct drv_data), GFP_KERNEL);
    if (!data){
        return -ENOMEM;
    }
    data -> device = device;
    pci_set_drvdata(device, data);

    int err = pci_enable_device(device);
    if (err){
        printk("[FPGA] ERROR WHILE ENABLING DEVICE OCCURED: %d", err);
        goto err_enable;
    }
    else{
        printk("[FPGA] ENABLING DEVICE SUCCESSFULL");
    }
    err = pci_request_region(device, 0, DRIVER_NAME);
    if (err){
        printk("[FPGA] ERROR WHILE REQUESTING MEM REGION OCCURED: %d", err);
        goto err_request_regions;
    }
    else{
        printk("[FPGA] REGION REQUEST SUCCESSFULL");
    }

    data->bar0 = pci_iomap(device, 0, CNT_ALLOCATED_BYTES);
    if(!(data -> bar0)) {
        printk("[FPGA] ERROR WHILE MAPPING");
        goto err_mapping;
    }
    else{
        printk("[FPGA] MAPPING SUCCESSFULL");
    }

    int alloc_vec_cnt = pci_alloc_irq_vectors(device, VEC_CNT, VEC_CNT, PCI_IRQ_MSI);
    if (alloc_vec_cnt != VEC_CNT){
        printk("[FPGA] ERROR WHILE ALLOCATING IRQ VECTORS");
        goto err_alloc_vec;
    }
    else{
        printk("[FPGA] VECTOR ALLOCATION SUCCESSFULL");
    }

    int turn_leds_vec = pci_irq_vector(device, 0);
    printk("[FPGA] IRQ VECTORS ALLOCATED: TURN ON - %d", turn_leds_vec);

    err = request_irq(turn_leds_vec, turn_leds_handler, 0,  "FPGA leds on", data);
    if (err){
        printk("[FPGA] ERROR WHILE IRQ REQUEST");
        goto err_irq_request;
    }
    else{
        printk("[FPGA] IRQ REGISTER SUCCESSFULL");
    }

    return 0;
    
    err_irq_request:
        free_irq(turn_leds_vec, data);
        printk("[FPGA] UNREGISTER IRQ");
    
    err_alloc_vec:
        pci_free_irq_vectors(device);
        printk("[FPGA] FREE IRQ VECTORS");
    
    err_mapping:
        pci_iounmap(device, data->bar0);
        printk("[FPGA] IOUNMAP");

    err_request_regions:
        pci_release_region(device, 0);
        printk("[FPGA] RELEASE REGION");
    
    err_enable:
        kfree(data);
        pci_disable_device(device);
        printk("[FPGA] DATA FREE & DISABLING DEVICE");

    return err;
}

static void fpga_remove(struct pci_dev* device){
    printk("[FPGA] REMOVE FUNC CALLED");

    struct drv_data* data = pci_get_drvdata(device);
    
    free_irq(pci_irq_vector(device, 0), data);

    pci_free_irq_vectors(device);

    pci_iounmap(device, data->bar0);
    pci_release_region(device, 0);
    pci_disable_device(device);

    kfree(data);
    pci_set_drvdata(device, NULL);
}

struct pci_device_id fpga_pci_id_table[] = {
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
