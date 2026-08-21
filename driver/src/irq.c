#include <linux/irq.h>
#include <linux/pci.h>
#include "irq.h"
#include "pci.h"
#include "registers.h"
#include <linux/wait.h>


static void fpga_irq_ack(void __iomem* bar[]){
    iowrite32(1, bar[BAR_AXI_LITE_IDX] + ACK_REG_OFS);
    iowrite32(0, bar[BAR_AXI_LITE_IDX] + ACK_REG_OFS);
}


static irqreturn_t irq_handler(int irq, void* inp_data){
    printk("[FPGA] irq #%d detected\n", irq);
    struct drv_data* data = (struct drv_data*)inp_data;

    data->is_irq = true;
    wake_up_interruptible(&data->wq);

    fpga_irq_ack(data->bar);

    return IRQ_HANDLED;
}


int fpga_init_irq(struct pci_dev* device){
    struct drv_data* data = dev_get_drvdata(&device->dev); 
    int err;
    
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

    return 0;
}

