#define _GNU_SOURCE

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <tcpreplay/tcpreplay_api.h>

#include "optimization.h"
#include "sender_tcpreplay.h"
#include "traffic_sender.h"

#define DEVICE_TO_LISTEN "/dev/fpga_cdev_sub"
#define INTERFACE_TO_REPLAY "ens3"
#define PCAP_PATH "data/traffic.pcap"


int main() {
    if (optimization() != 0) {
        return 1;
    }
    printf("USERSPACE APP STARTED\n");
    int fd = open(DEVICE_TO_LISTEN, O_RDONLY);
    if (fd < 0) {
        printf("Error while opening file descriptor %d\n", fd);
        return 1;
    }

    struct traffic_sender sender = {0};

    if (traffic_sender_init_tcpreplay(
        &sender,
        INTERFACE_TO_REPLAY,
        PCAP_PATH) < 0) {
        close(fd);
        return 1;
    }

    while (true) {
        uint64_t kernel_irq_time;
        ssize_t ret = read(fd, &kernel_irq_time, sizeof(kernel_irq_time));

        // Проверяем, что вернулось ровно 8 байт
        if (ret != sizeof(kernel_irq_time)) {
            if (ret < 0) {
                perror("read /dev/fpga");
            } else {
                printf("Read error: expected %zu bytes, got %zd\n", sizeof(kernel_irq_time), ret);
            }
            goto cleanup;
        }
        
        // Замеряет время от пробуждения программы до вызова tcpreplay_replay().
        // Это полезно, чтобы понять, не тормозит ли сам софт.
        
        // первая метка
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        if (sender.send(sender.ctx) < 0){
            goto cleanup;
        }

        // вторая метка
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        uint64_t local_delta_ns =
            (ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL + (ts_end.tv_nsec - ts_start.tv_nsec);
        double local_delta_us = local_delta_ns / 1000.0;

        // 2. Переводим время окончания в абсолютные наносекунды
        uint64_t user_end_time_ns = (uint64_t)ts_end.tv_sec * 1000000000ULL + ts_end.tv_nsec;

        // 3. Считаем общее время (от прерывания в ядре до конца replay)
        uint64_t total_latency_ns = user_end_time_ns - kernel_irq_time;
        double total_latency_us = total_latency_ns / 1000.0;

        // Выводим оба результата
        printf(">>> TOTAL LATENCY (IRQ to TX): %llu ns (%.2f us)\n",    // Показывает реальную сквозную задержку.
               (unsigned long long)total_latency_ns, total_latency_us); // Должна быть < 30 мкс

        printf(">>> LOCAL LATENCY (Wake to TX): %llu ns (%.2f us) <<<\n", // Показывает, сколько времени тратит сам
               (unsigned long long)local_delta_ns, local_delta_us);       // tcpreplay на отправку пакета после того,
                                                                          //как ОС его разбудила

        const tcpreplay_stats_t *stats = tcpreplay_get_stats(sender.ctx);
        printf("total packets sent: %llu\n", stats->pkts_sent);
    }

cleanup:
    close(fd);
    sender.destroy(sender.ctx);
    return -1;
}
