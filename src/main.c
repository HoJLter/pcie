#include <linux/pci.h>
#include <linux/module.h>

#define VENDOR_ID 0x10EE 
#define DEVICE_ID 0x1234 
#define SUBVENDOR_ID 0x10EE
#define SUBDEVICE_ID 0x5678



struct pci_device_id fpgaPciIdTable{
    .vendor = VENDOR_ID,
    .device = DEVICE_ID,
    .subvendor = SUBVENDOR_ID,
    .subdevice = SUBDEVICE_ID
}

struct pci_driver fpgaPciDriver = {
    .name = "Kintex-7 Driver",
    .id_table = &fpgaPciIdTable,
    .probe = fpgaProbe,
    remove = fpgaRemove 
};


int fpgaProbe(struct pci_dev* device){
    int err = pci_enable_device(device);
    if (err){
        return err;
    }
    printk("wassup");
}