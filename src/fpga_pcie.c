#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>

#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021
#define XDMA_BAR_CNT 6


struct drv_data {
    void __iomem* bar[XDMA_BAR_CNT];
    struct pci_dev *pdev;

    int config_bar_idx;
    int axi_bar_idx;
};


int probe(struct pci_dev* device, const struct pci_device_id *ent){
    struct drv_data* data = devm_kzalloc(&device->dev, sizeof(struct drv_data), GFP_KERNEL);
    if (!data){
        return -ENOMEM;
    }
    pr_info("[FPGA] driver probe function");
}


static void remove(struct pci_dev* device){
    pr_info("[FPGA] driver remove function");
}

const struct pci_device_id id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {0}
};

MODULE_DEVICE_TABLE(pci, id_table);

struct pci_driver driver = {
    .name = "Kintex-7 PCIe driver",
    .probe = probe,
    .remove = remove,
    .id_table = id_table
};

static int __init fpga_init(void){
    pr_info("[FPGA] init function called");
    return pci_register_driver(&driver);
}

static void __exit fpga_exit(void){
    pr_info("[FPGA] exit function called");
    pci_unregister_driver(&driver);
}

module_init()
module_exit()