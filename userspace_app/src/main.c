#define _GNU_SOURCE

#include <bits/types/struct_sched_param.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <tcpreplay/tcpreplay_api.h>
#include <time.h>
#include <unistd.h>

#define DEVICE_TO_LISTEN "/dev/fpga_cdev_sub"
#define INTERFACE_TO_REPLAY "ens3"
#define PCAP_PATH "data/traffic.pcap"

void preload_pcap_file(tcpreplay_t *ctx,
                       int idx); // без этого tcpreplay будет открывать
// и читать файл traffic.pcap с диска каждый раз, когда приходит прерывание.

int optimization() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("Ошибка mlockall");
    }

    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    CPU_SET(2, &cpuset);
    CPU_SET(4, &cpuset);

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
        perror("Ошибка при привязке к ядрам (sched_setaffinity)");
    } else {
        printf("Программа успешно привязана к ядрам 2 и 4!\n");
    }

    int32_t latency = 0;
    int dma_fd = open("/dev/cpu_dma_latency", O_WRONLY);
    if (dma_fd >= 0) {
        write(dma_fd, &latency, sizeof(latency));
    }

    struct sched_param param;
    param.sched_priority = 99;

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        fprintf(stderr, "Ошибка при установке SCHED_FIFO: %s\n",
                strerror(errno));
        return 1;
    }

    printf("Политика SCHED_FIFO с приоритетом %d успешно применена!\n",
           param.sched_priority);
    return 0;
}

int main() {
    if (optimization() != 0) {
        return 1;
    }
    printf("USERSPACE APP STARTED\n");
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        printf("Error while opening file descriptor %d\n", fd);
        return 1;
    }

    tcpreplay_t *ctx = tcpreplay_init();

    // Включаем предзагрузку ДО prepare
    tcpreplay_set_preload_pcap(ctx, true);

    if (tcpreplay_set_interface(ctx, intf1, INTERFACE_TO_REPLAY) < 0 ||
        tcpreplay_add_pcapfile(ctx, PCAP_PATH) < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0) {
        printf("setup failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }
    printf("SETUP PASSED\n");

    if (tcpreplay_prepare(ctx) < 0) {
        printf("prepare failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }
    printf("PREPARE PASSED\n");

    // Принудительно грузим pcap в память
    preload_pcap_file(ctx, 0);

    while (true) {
        uint64_t kernel_irq_time;
        ssize_t ret = read(fd, &kernel_irq_time, sizeof(kernel_irq_time));

        // Проверяем, что вернулось ровно 8 байт
        if (ret != sizeof(kernel_irq_time)) {
            if (ret < 0) {
                perror("read /dev/fpga");
            } else {
                printf("Read error: expected %zu bytes, got %zd\n",
                       sizeof(kernel_irq_time), ret);
            }
            goto cleanup;
        }

        // амеряет время от пробуждения программы до вызова tcpreplay_replay().
        //  Это полезно, чтобы понять, не тормозит ли сам софт.
        // первая метка
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        if (tcpreplay_set_loop(ctx, 1) < 0) {
            printf("set loop failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        if (tcpreplay_replay(ctx) < 0) {
            printf("replay failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }
        
        //вторая метка
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        uint64_t local_delta_ns =
            (ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL +
            (ts_end.tv_nsec - ts_start.tv_nsec);
        double local_delta_us = local_delta_ns / 1000.0;

        // 2. Переводим время окончания в абсолютные наносекунды
        uint64_t user_end_time_ns =
            (uint64_t)ts_end.tv_sec * 1000000000ULL + ts_end.tv_nsec;

        // 3. Считаем общее время (от прерывания в ядре до конца replay)
        uint64_t total_latency_ns = user_end_time_ns - kernel_irq_time;
        double total_latency_us = total_latency_ns / 1000.0;

        // Выводим оба результата
        printf(
            ">>> TOTAL LATENCY (IRQ to TX): %llu ns (%.2f us)\n", // Показывает
                                                                  // реальную
                                                                  // сквозную
                                                                  // задержку.
            (unsigned long long)total_latency_ns,
            total_latency_us); // Должна быть < 30 мкс

        printf(
            ">>> LOCAL LATENCY (Wake to TX): %llu ns (%.2f us) <<<\n", // Показывает,
                                                                       // сколько
                                                                       // времени
                                                                       // тратит
                                                                       // сам
            (unsigned long long)local_delta_ns,
            local_delta_us); // tcpreplay на отправку пакета после того,
                             // как ОС его разбудила

        const tcpreplay_stats_t *stats = tcpreplay_get_stats(ctx);
        printf("total packets sent: %d\n", stats->pkts_sent);
    }

cleanup:
    close(fd);
    tcpreplay_close(ctx);
    return -1;
}
