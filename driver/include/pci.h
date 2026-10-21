#ifndef PCI_H
#define PCI_H

#include <linux/io.h>
#include <linux/wait.h>
#include <linux/cdev.h>

#define DEVICE_CLASS_NAME "fpga-class"
#define DRIVER_NAME "Kintex-7 PCIe driver"
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7011

#define XDMA_BAR_CNT 6
#define BAR_AXI_LITE_IDX 0
#define BAR_CFG_IDX 1

/**
 * Информация драйвера, специфичная для каждого отдельно устройства
 *
 * @bar: Массив адресов BAR'ов, замапленных в виртуальную память хоста
 * @irq_number: Номер IRQ вектора, выделенный системой
 * @char_dev: character device, связывает обращения к /dev/fpga с file_operations из cdev.c
 * @dev_id: Номер символьного устройства. Состоит из двух чисел: MAJOR и MINOR
 * @wq: Очередь ожидания
 * @is_irq: Обрабатывается ли сейчас прерывание
 */
struct device_data {
    void __iomem* bar[XDMA_BAR_CNT];

    int irq_number;

    struct cdev char_dev;
    dev_t dev_id;

    wait_queue_head_t wq;
    bool is_irq;
};


/**
 * Глобальная информация драйвера
 *
 * @device_class: класс всех устройств, подключенных к драйверу. Используется в cdev.c
 */
struct global_drv_data {
    struct class* device_class;
};

extern const struct attribute_group *fpga_groups[];

#endif
