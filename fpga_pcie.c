#include <linux/pci.h>
#include <linux/module.h>

#define DRIVER_NAME "Kintex-7 Driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define CNT_ALLOCATED_BYTES 8192

void* __iomem bar0;

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

    bar0 = pci_iomap(device, 0, CNT_ALLOCATED_BYTES);

    u32 data_to_write = 0x1234567;
    writel(data_to_write, bar0 + 0x10);

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

MODULE_DEVICE_TABLE(pci, fpgaPciIdTable);

struct pci_driver fpgaPciDriver = {
    .name = DRIVER_NAME,
    .id_table = fpgaPciIdTable,
    .probe = fpgaProbe,
    .remove = fpgaRemove 
};

int __init fpga_init(void){
    printk("[FPGA] start init 1");
    return pci_register_driver(&fpgaPciDriver);
}

void __exit fpga_exit(void){
    printk("[FPGA] start exit 1");
    u32 result = readl(bar0 + 0x10);
    printk("[FPGA] DATA HAS BEEN READED: %x", result);
    pci_unregister_driver(&fpgaPciDriver);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");
