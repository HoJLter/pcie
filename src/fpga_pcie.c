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

#define DRIVER_NAME "Kintex-7 PCIe driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define XDMA_BAR_CNT 6
#define BAR_AXI_LITE_IDX 0
#define BAR_CFG_IDX 2

#define IRQ_BLOCK_OFS (0x2 << 12)
#define IRQ_BLOCK_ID_OFS 0x0
#define IRQ_ENABLE_W1S_OFS 0x08
#define IRQ_ENABLE_W1C_OFS 0x0C
#define IRQ_USER_VECTOR_OFS 0x80

#define ACK_REG_OFS 0x0
#define LED_REG_OFS 0x4

struct drv_data {
    void __iomem* bar[XDMA_BAR_CNT];
    struct pci_dev *pdev;

    int irq_number;
};


static irqreturn_t irq_handler(int irq, void* inp_data){
    printk("[FPGA] irq #%d detected\n", irq);
    struct drv_data* data = (struct drv_data*)inp_data;
    
    u32 cur_led_value = ioread32(data->bar[BAR_AXI_LITE_IDX] + LED_REG_OFS);
    iowrite32(~cur_led_value, data->bar[BAR_AXI_LITE_IDX] + LED_REG_OFS);
    
    iowrite32(1, data->bar[BAR_AXI_LITE_IDX] + ACK_REG_OFS);
    iowrite32(0, data->bar[BAR_AXI_LITE_IDX] + ACK_REG_OFS);

    return IRQ_HANDLED;
}


static int probe(struct pci_dev *device, const struct pci_device_id *ent) {
    int err;
    void __iomem * const *iomap;
    struct drv_data *data;

    pr_info("[FPGA] probe start\n");

    data = devm_kzalloc(&device->dev, sizeof(*data), GFP_KERNEL);
    if (!data) {
        pr_err("[FPGA] devm_kzalloc failed\n");
        return -ENOMEM;
    }
    pci_set_drvdata(device, data);


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
    pr_info("[FPGA] regions mapped\n");


    iomap = pcim_iomap_table(device);
    if (!iomap) {
        pr_err("[FPGA] pcim_iomap_table returned NULL\n");
        return -ENOMEM;
    }
    data->bar[BAR_CFG_IDX] = iomap[BAR_CFG_IDX];
    data->bar[BAR_AXI_LITE_IDX] = iomap[BAR_AXI_LITE_IDX];
    if (!data->bar[BAR_CFG_IDX]) {
        pr_err("[FPGA] CFG BAR is NULL\n");
        return -ENOMEM;
    }
    if (!data->bar[BAR_AXI_LITE_IDX]) {
        pr_err("[FPGA] AXI BAR is NULL\n");
        return -ENOMEM;
    }
    pr_info("[FPGA] BARs valid\n");


    err = pci_alloc_irq_vectors(device, 1, 1, PCI_IRQ_MSI);
    if (err < 0) {
        pr_err("[FPGA] pci_alloc_irq_vectors failed: %d\n", err);
        return err;
    }
    pr_info("[FPGA] IRQ vectors allocated: %d\n", err);


    data->irq_number = pci_irq_vector(device, 0);
    err = devm_request_irq(&device->dev, data->irq_number, irq_handler, 0, "xilinx", data);
    if (err) {
        pr_err("[FPGA] devm_request_irq failed: %d\n", err);
        return err;
    }

    
    pr_info("[FPGA] IDENTIFIER: 0x%x\n", ioread32(data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_BLOCK_ID_OFS));

    iowrite32(BIT(0), data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_ENABLE_W1S_OFS);
    pr_info("[FPGA] interrupts on card enabled\n");
    
    pr_info("[FPGA] vector before writing: %d\n", ioread32(data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_USER_VECTOR_OFS));
    iowrite32(0, data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_USER_VECTOR_OFS);
    pr_info("[FPGA] vector after writing: %d\n", ioread32(data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_USER_VECTOR_OFS));

    pr_info("[FPGA] probe done\n");

    return 0;
}

static void remove(struct pci_dev* device){
    pr_info("[FPGA] driver remove function\n");

    pci_clear_master(device);
    pr_info("[FPGA] Master flag clear\n");

    pci_free_irq_vectors(device);
    pr_info("[FPGA] IRQ vectors free\n");
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
    pr_info("[FPGA] init function called\n");
    return pci_register_driver(&driver);
}

static void __exit fpga_exit(void){
    pr_info("[FPGA] exit function called\n");
    pci_unregister_driver(&driver);
}

module_init(fpga_init);
module_exit(fpga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("???");