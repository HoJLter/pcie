#ifndef FPGA_UAPI_H
#define FPGA_UAPI_H

#include <linux/types.h>

#define FPGA_EVENT_ABI_VERSION 1u

/**
 * struct fpga_irq_event - одно наблюдение прерывания, возвращаемое read(2)
 *
 * @abi_version: версия ABI (FPGA_EVENT_ABI_VERSION)
 * @size:        sizeof(struct fpga_irq_event), проверяется в userspace
 * @seq:         порядковый номер прерывания (32 бита, переполняется)
 * @missed:      сколько прерываний было перезаписано с прошлого успешного read
 * @irq_ts_ns:   CLOCK_MONOTONIC в наносекундах на входе в обработчик
 * @read_ts_ns:  CLOCK_MONOTONIC в наносекундах в момент выдачи события
 * @latency_ns:  read_ts_ns - irq_ts_ns, вычисляется в ядре
 *
 * Все временные метки используют CLOCK_MONOTONIC (ktime_get_ns()).
 * Структура имеет фиксированный размер, не содержит указателей и padding,
 * поэтому одинакова для 32- и 64-битного userspace.
 */
struct fpga_irq_event {
    __u32 abi_version;
    __u32 size;
    __u32 seq;
    __u32 missed;
    __u64 irq_ts_ns;
    __u64 read_ts_ns;
    __u64 latency_ns;
};

#endif /* FPGA_UAPI_H */
