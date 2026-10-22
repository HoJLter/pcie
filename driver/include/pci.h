#ifndef PCI_H
#define PCI_H

#include <linux/io.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "fpga_uapi.h"

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
 * @char_dev: character device, связывает обращения к /dev/fpga_cdev_sub
 *            с file_operations из cdev.c
 * @dev_id: Номер символьного устройства. Состоит из двух чисел: MAJOR и MINOR
 * @wq: Очередь ожидания события прерывания
 * @event_lock: Защищает @event, @irq_seq и @consumed_seq
 * @event: Последнее наблюдение прерывания, публикуется в hardirq
 * @irq_seq: Счётчик прерываний (32 бита, переполняется)
 * @consumed_seq: Номер последнего события, выданного в userspace
 * @reg_lock: Защищает read-modify-write регистра CONTROL_REG
 *            (подтверждение IRQ и sysfs detect_reg)
 * @open_lock: Сериализует open/release и защищает @opened
 * @opened: Признак того, что устройство уже открыто (один потребитель)
 */
struct device_data {
    void __iomem* bar[XDMA_BAR_CNT];

    int irq_number;

    struct cdev char_dev;
    dev_t dev_id;

    wait_queue_head_t wq;

    spinlock_t event_lock;
    struct fpga_irq_event event;
    u32 irq_seq;
    u32 consumed_seq;

    spinlock_t reg_lock;

    struct mutex open_lock;
    bool opened;
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
