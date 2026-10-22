#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tcpreplay/tcpreplay_api.h>

#include "fpga_uapi.h"

_Static_assert(sizeof(struct fpga_irq_event) == 40, "fpga ABI size");

#define INTERFACE_TO_REPLAY "ens3"
#define DEVICE_PATH "/dev/fpga_cdev_sub"
#define PCAP_PATH "data/traffic.pcap"

static uint64_t now_ns(clockid_t clk)
{
    struct timespec ts;

    if (clock_gettime(clk, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    struct fpga_irq_event ev;
    tcpreplay_t* ctx;
    int fd = -1;

    ctx = tcpreplay_init();
    if (!ctx) {
        printf("tcpreplay_init failed\n");
        return 1;
    }

    if (
        tcpreplay_set_interface(ctx, intf1, INTERFACE_TO_REPLAY) < 0 ||
        tcpreplay_add_pcapfile(ctx, PCAP_PATH) < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0
    ) {
        printf("setup failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }
    printf("SETUP PASSED\n");

    if (tcpreplay_prepare(ctx) < 0) {
        printf("prepare failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }
    printf("PREPARE PASSED\n");

    /*
     * Открываем устройство только после потенциально долгой подготовки
     * tcpreplay. fpga_open() отбрасывает прерывания, пришедшие до open(),
     * поэтому первый read() не вернёт событие времени подготовки pcap.
     */
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        printf("Error while opening %s: %s\n", DEVICE_PATH, strerror(errno));
        goto cleanup;
    }

    while (1) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        uint64_t t_read, t_done, total_ns;
        int64_t skew_ns, abs_ns;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            printf("Read error: %s\n", strerror(errno));
            goto cleanup;
        }
        if (n != (ssize_t)sizeof(ev)) {
            printf("Short read: %zd (expected %zu)\n", n, sizeof(ev));
            goto cleanup;
        }
        if (ev.abi_version != FPGA_EVENT_ABI_VERSION ||
            ev.size != sizeof(ev)) {
            printf("ABI mismatch: version=%u size=%u\n",
                   ev.abi_version, ev.size);
            goto cleanup;
        }

        t_read = now_ns(CLOCK_MONOTONIC);

        if (tcpreplay_set_loop(ctx, 1) < 0) {
            printf("set loop failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        if (tcpreplay_replay(ctx) < 0) {
            printf("replay failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        t_done = now_ns(CLOCK_MONOTONIC);

        /*
         * Если приложение и драйвер в одном time namespace, абсолютная
         * разность точнее: она включает copy_to_user и возврат из syscall.
         * В другом time namespace CLOCK_MONOTONIC смещён, поэтому используем
         * сумму kernel-задержки и пользовательского интервала после read().
         */
        skew_ns = (int64_t)t_read - (int64_t)ev.read_ts_ns;
        abs_ns = (int64_t)t_done - (int64_t)ev.irq_ts_ns;
        if (llabs(skew_ns) < 1000000 && abs_ns >= 0)
            total_ns = (uint64_t)abs_ns;
        else
            total_ns = ev.latency_ns + (t_done - t_read);

        /*
         * Печать находится после t_done и не входит в текущий замер, но
         * влияет на следующий цикл. Для маложиттерных серий лучше писать
         * выборку в файл/буфер, а не в консоль.
         */
        printf("seq=%" PRIu32 " missed=%" PRIu32
               " kernel=%" PRIu64 " us"
               " post_read=%" PRIu64 " us"
               " total=%" PRIu64 " us\n",
               ev.seq, ev.missed,
               (uint64_t)(ev.latency_ns / 1000),
               (uint64_t)((t_done - t_read) / 1000),
               total_ns / 1000);
    }

cleanup:
    if (fd >= 0)
        close(fd);
    tcpreplay_close(ctx);
    return 1;
}
