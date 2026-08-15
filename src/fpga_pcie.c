#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>

#define DRIVER_NAME "Kintex-7 PCIe driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define XDMA_BAR_CNT 6
#define BAR_AXI_LITE_IDX 0
#define BAR_CFG_IDX 2

#define IRQ_BLOCK_IDENTIFIER 0x06
#define IRQ_BLOCK_OFS (0x2 << 12)
#define IRQ_BLOCK_ID_OFS 0x0
#define IRQ_ENABLE_MASK_OFS 0x04

struct drv_data {
    void __iomem* bar[XDMA_BAR_CNT];
    struct pci_dev *pdev;

    int irq_number;
};


static irqreturn_t irq_handler(int irq, void* dev){
    printk("[FPGA] irq detected");
    return IRQ_HANDLED;
}


int probe(struct pci_dev* device, const struct pci_device_id *ent){
    pr_info("[FPGA] driver probe function");

    int err;

    struct drv_data* data = devm_kzalloc(&device->dev, sizeof(struct drv_data), GFP_KERNEL);
    if (!data){
        pr_err("[FPGA] drv data allocation fail");
        return -ENOMEM;
    }
    pci_set_drvdata(device, data);

    err = pcim_enable_device(device);
    if (err){
        pr_err("[FPGA] enabling device fail");
        return err;
    }
    err = pcim_iomap_regions(device, BIT(BAR_CFG_IDX) | BIT(BAR_AXI_LITE_IDX), DRIVER_NAME);
    if (err)
        return err;

    void __iomem** iomap = pcim_iomap_table(device);
    if (!iomap)
        return -ENOMEM;

    data->bar[BAR_CFG_IDX] = iomap[BAR_CFG_IDX];
    data->bar[BAR_AXI_LITE_IDX] = iomap[BAR_AXI_LITE_IDX];

    if (!data->bar[BAR_CFG_IDX] ||
        !data->bar[BAR_AXI_LITE_IDX])
        return -ENOMEM;
    
    err = pci_alloc_irq_vectors(device, 1, 1, PCI_IRQ_MSI);
    if (err < 0){
        pr_err("[FPGA] allocation irq vectors fail");
        return -ENOMEM;
    }

    data->irq_number = pci_irq_vector(device, 0);

    err = devm_request_irq(&device->dev, data->irq_number, irq_handler, 0, "xilinx", data);
    if (err){
        pr_err("[FPGA] interrupt registration fail");
    }
    pr_info("[FPGA] IDENTIFIER: %x", ioread32(data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS));

    return 0;
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
    .name = DRIVER_NAME,
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

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");