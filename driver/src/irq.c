#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/ktime.h>
#include "irq.h"
#include "pci.h"
#include "registers.h"
#include <linux/wait.h>

/* Подтверждение обработки прерывания. Сериализовано с sysfs detect_reg. */
static void fpga_irq_ack(struct device_data* data){
    unsigned long flags;
    u32 val;

    spin_lock_irqsave(&data->reg_lock, flags);
    val = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    val |= HANDLED_ACK_MASK;
    iowrite32(val, data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);
}

/* Обработка прерывания. Публикует событие и будит ожидающих в wq. */
static irqreturn_t irq_handler(int irq, void* inp_data){
    struct device_data* data = (struct device_data*)inp_data;
    unsigned long flags;
    u64 irq_ts_ns;

    (void)irq;

    /*
     * Точка начала измерения: самое первое обращение драйвера к прерыванию.
     * До неё не должно быть printk, MMIO и других дорогих операций.
     */
    irq_ts_ns = ktime_get_ns();

    spin_lock_irqsave(&data->event_lock, flags);
    data->irq_seq++;
    data->event.irq_ts_ns = irq_ts_ns;
    data->event.read_ts_ns = 0;
    data->event.latency_ns = 0;
    data->event.seq = data->irq_seq;
    spin_unlock_irqrestore(&data->event_lock, flags);

    fpga_irq_ack(data);
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
        pci_free_irq_vectors(device);
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
