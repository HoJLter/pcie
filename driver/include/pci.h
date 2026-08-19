#ifndef PCI_H
#define PCI_H

#include <linux/io.h>

#define DRIVER_NAME "Kintex-7 PCIe driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7021

#define XDMA_BAR_CNT 6
#define BAR_AXI_LITE_IDX 0
#define BAR_CFG_IDX 2

struct drv_data {
    void __iomem* bar[XDMA_BAR_CNT];
    struct pci_dev *pdev;

    int irq_number;
};

#endif