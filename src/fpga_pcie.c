#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include "registers.h"

#define DRIVER_NAME "Kintex-7 Driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define CNT_ALLOCATED_BYTES 128

#define VEC_CNT 4


struct drv_data {
    void __iomem* bar0;
    struct pci_dev* device; 
};

static irqreturn_t turn_leds_handler(int irq, void* dev){
    printk(KERN_INFO "[FPGA] | [INFO] | IRQ DETECTED: TURN_LEDS");
    struct drv_data* data = dev;
    u32 is_turned_on = readl((data -> bar0) + LEDS_REG);
    printk(KERN_INFO "[FPGA] | [INFO] | DATA READED: %d", is_turned_on);

    if (is_turned_on) writel(0, (data -> bar0) + LEDS_REG);
    else writel(1, (data -> bar0) + LEDS_REG);

    return IRQ_HANDLED;
}

static irqreturn_t test1_handler(int irq, void* dev){
    printk(KERN_INFO "[FPGA] | [INFO] | IRQ DETECTED: TESTS1");
    return IRQ_HANDLED;
}

static int fpga_probe(struct pci_dev* device, const struct pci_device_id *ent){
    printk(KERN_INFO "[FPGA] | [INFO] | BAR0 size = %llu\n", (unsigned long long)pci_resource_len(device, 0));

    struct drv_data* data = kzalloc(sizeof(struct drv_data), GFP_KERNEL);
    if (!data){
        return -ENOMEM;
    }
    data -> device = device;
    pci_set_drvdata(device, data);

    int err = pci_enable_device(device);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE ENABLING DEVICE OCCURED: %d", err);
        goto err_enable;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | ENABLING DEVICE SUCCESSFULL");
    }
    err = pci_request_region(device, 0, DRIVER_NAME);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE REQUESTING MEM REGION OCCURED: %d", err);
        goto err_request_regions;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | REGION REQUEST SUCCESSFULL");
    }

    data->bar0 = pci_iomap(device, 0, CNT_ALLOCATED_BYTES);
    if(!(data -> bar0)) {
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE MAPPING");
        goto err_mapping;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | MAPPING SUCCESSFULL");
    }

    int alloc_vec_cnt = pci_alloc_irq_vectors(device, VEC_CNT, VEC_CNT, PCI_IRQ_MSIX);
    printk("[FPGA] | [DEBUG] | Alloc_vec_cnt = %d", alloc_vec_cnt);
    if (alloc_vec_cnt < 0){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE ALLOCATING IRQ VECTORS");
        goto err_alloc_vec;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | VECTOR ALLOCATION SUCCESSFULL");
    }

    int turn_leds_vec = pci_irq_vector(device, 0);
    int test1_vec = pci_irq_vector(device, 1);
    int test2_vec = pci_irq_vector(device, 2);
    int test3_vec = pci_irq_vector(device, 3);


    printk(KERN_INFO "[FPGA] | [INFO] | IRQ VECTORS ALLOCATED: TURN ON - %d; TEST1 - %d; TEST2 - %d TEST 3 - %d", turn_leds_vec, test1_vec, test2_vec, test3_vec);

    err = request_irq(turn_leds_vec, turn_leds_handler, 0,  "FPGA leds on", data);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE IRQ REQUEST");
        goto err_irq_request;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | TURN ON IRQ REGISTER SUCCESSFULL");
    }

    err = request_irq(test1_vec, test1_handler, 0,  "FPGA test1", data);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE IRQ REQUEST");
        goto err_irq_request;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | IRQ TEST1 REGISTER SUCCESSFULL");
    }

    err = request_irq(test2_vec, test1_handler, 0,  "FPGA test2", data);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE IRQ REQUEST");
        goto err_irq_request;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | IRQ TEST2 REGISTER SUCCESSFULL");
    }

    err = request_irq(test3_vec, test1_handler, 0,  "FPGA test3", data);
    if (err){
        printk(KERN_ERR "[FPGA] | [ERROR] | ERROR WHILE IRQ REQUEST");
        goto err_irq_request;
    }
    else{
        printk(KERN_INFO "[FPGA] | [INFO] | IRQ TEST3 REGISTER SUCCESSFULL");
    }

    return 0;
    
    err_irq_request:
        free_irq(turn_leds_vec, data);
        free_irq(test1_vec, data);
        printk(KERN_WARNING "[FPGA] | [WARNING] | UNREGISTER IRQ");
    
    err_alloc_vec:
        pci_free_irq_vectors(device);
        printk(KERN_WARNING "[FPGA] | [WARNING] | FREE IRQ VECTORS");
    
    err_mapping:
        pci_iounmap(device, data->bar0);
        printk(KERN_WARNING "[FPGA] | [WARNING] | IOUNMAP");

    err_request_regions:
        pci_release_region(device, 0);
        printk(KERN_WARNING "[FPGA] | [WARNING] | RELEASE REGION");
    
    err_enable:
        if (data){
            kfree(data);
            printk(KERN_WARNING "[FPGA] | [WARNING] | DATA FREE");
        }
        else {
            printk(KERN_WARNING "[FPGA] | [WARNING] | DATA IS ALREADY NULLPTR");
        }
        if (device){
            pci_disable_device(device);
            printk(KERN_WARNING "[FPGA] | [WARNING] | DISABLING DEVICE");
        }
        else {
            printk(KERN_WARNING "[FPGA] | [WARNING] | DEVICE IS ALREADY NULLPTR");
        }

    return err;
}

static void fpga_remove(struct pci_dev* device){
    printk(KERN_INFO "[FPGA] | [INFO] | REMOVE FUNC CALLED");

    struct drv_data* data = pci_get_drvdata(device);
    
    free_irq(pci_irq_vector(device, 1), data);
    free_irq(pci_irq_vector(device, 0), data);

    pci_free_irq_vectors(device);
    if (data->bar0){
        pci_iounmap(device, data->bar0);
    }
    pci_release_region(device, 0);
    pci_disable_device(device);
    if(data){
        kfree(data);
    }
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
    printk(KERN_INFO "[FPGA] | [INFO] | INIT FUNCTION");
    return pci_register_driver(&fpga_pci_driver);
}

static void __exit fpga_exit(void){
    printk(KERN_INFO "[FPGA] | [INFO] | EXIT FUNCTION");

    pci_unregister_driver(&fpga_pci_driver);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");
