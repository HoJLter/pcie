#include <linux/irq.h>
#include <linux/pci.h>
#include "irq.h"
#include "pci.h"
#include "registers.h"
#include <linux/wait.h>
#include <linux/ktime.h>

/* Подтверждение обработки прерывания */
static void fpga_irq_ack(struct device_data* data){
    u32 val;
    val = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    val |= HANDLED_ACK_MASK; 
    iowrite32(val, data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
}

/* Обработка прерывания. Все участники очереди wq просыпаются */
static irqreturn_t irq_handler(int irq, void* inp_data){
    // пока убрал принт, тк он долгий
    //printk("[FPGA] irq #%d detected\n", irq);
    struct device_data* data = (struct device_data*)inp_data;

    data->irq_time = ktime_get();   //  фиксируем время
    
    fpga_irq_ack(data); //предложил поменять местами чтобы было побыстрее
    
    data->is_irq = true;
    wake_up_interruptible(&data->wq);

    return IRQ_HANDLED;
}

/* Базовая инициализация прерываний */
int fpga_init_irq(struct pci_dev* device){
    struct device_data* data = dev_get_drvdata(&device->dev); 
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

    // Идентификатор блока IRQ в конфигурации XDMA. Должен быть равен 0x1fc20004 при успешной инициализации
    pr_info("[FPGA] IDENTIFIER: 0x%x\n", ioread32(data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_BLOCK_ID_OFS));

    // Настройка конфигурационных регистров FPGA

    // Настройка маски прерываний XDMA
    iowrite32(BIT(0), data->bar[BAR_CFG_IDX] + IRQ_BLOCK_OFS + IRQ_ENABLE_W1S_OFS);
    pr_info("[FPGA] interrupts on card enabled\n");
    
    return 0;
}

