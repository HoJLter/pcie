#include <linux/pci.h>
#include <linux/module.h>

#define DRIVER_NAME "Kintex-7 Driver"
#define VENDOR_ID 0x10EE 
#define DEVICE_ID 0x1234 





int fpgaProbe(struct pci_dev* device, const struct pci_device_id *ent){
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

    void* __iomem bar0 = pci_iomap(device, 0, 1);
    
    printk("[FPGA] DEVICE ENABLED AND BAR0 REQUESTED");

    return 0;
}

void fpgaRemove(struct pci_dev* device){
    printk("[FPGA] REMOVE FUNC CALLED");

    pci_release_region(device, 0);
    pci_disable_device(device);
}

struct pci_device_id fpgaPciIdTable[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {0}
};

struct pci_driver fpgaPciDriver = {
    .name = DRIVER_NAME,
    .id_table = fpgaPciIdTable,
    .probe = fpgaProbe,
    .remove = fpgaRemove 
};

int __init fpga_init(void){
    return pci_register_driver(&fpgaPciDriver);
}

void __exit fpga_exit(void){
    pci_unregister_driver(&fpgaPciDriver);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");