#include <tcpreplay/tcpreplay_api.h>
#include <time.h>
#include <stdint.h>

#define DEVICE_TO_LISTEN "/dev/fpga"
#define INTERFACE_TO_REPLAY "ens3"
#define DEVICE_PATH "/dev/fpga"
#define PCAP_PATH "data/traffic.pcap"

void preload_pcap_file(tcpreplay_t* ctx, int idx); //без этого tcpreplay будет открывать
//и читать файл traffic.pcap с диска каждый раз, когда приходит прерывание.

int main(){
    printf("USERSPACE APP STARTED\n");
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0){
        printf("Error while opening file descriptor %d\n", fd);
        return 1;
    }

    tcpreplay_t* ctx = tcpreplay_init();
    
    // Включаем предзагрузку ДО prepare
    tcpreplay_set_preload_pcap(ctx, true);

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

    // Принудительно грузим pcap в память
    preload_pcap_file(ctx, 0);

    while (true){
        uint64_t kernel_irq_time;
        int err = read(fd, &kernel_irq_time, sizeof(kernel_irq_time));
        if (err < 0){
            printf("Read error: %d\n", err);
            perror("read /def/fpga");
            goto cleanup;
        }

        //амеряет время от пробуждения программы до вызова tcpreplay_replay().
        // Это полезно, чтобы понять, не тормозит ли сам софт.
        //первая метка
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start);

        if (tcpreplay_set_loop(ctx, 1) < 0){
            printf("set loop failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        if (tcpreplay_replay(ctx) < 0) {
            printf("replay failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }
        
        //вторая метка
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_end);

        uint64_t local_delta_ns = (ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL +
            (ts_end.tv_nsec - ts_start.tv_nsec);
        double local_delta_us = local_delta_ns / 1000.0;

        // 2. Переводим время окончания в абсолютные наносекунды
        uint64_t user_end_time_ns = (uint64_t)ts_end.tv_sec * 1000000000ULL + ts_end.tv_nsec;

        // 3. Считаем общее время (от прерывания в ядре до конца replay)
        uint64_t total_latency_ns = user_end_time_ns - kernel_irq_time;
        double total_latency_us = total_latency_ns / 1000.0;

        // Выводим оба результата
        printf(">>> TOTAL LATENCY (IRQ to TX): %llu ns (%.2f us)\n",    // Показывает реальную сквозную задержку.
            (unsigned long long)total_latency_ns, total_latency_us);    // Должна быть < 30 мкс
        
        printf(">>> LOCAL LATENCY (Wake to TX): %llu ns (%.2f us) <<<\n",   // Показывает, сколько времени тратит сам 
            (unsigned long long)local_delta_ns, local_delta_us);    // tcpreplay на отправку пакета после того,
                                                                    //как ОС его разбудила


        const tcpreplay_stats_t *stats = tcpreplay_get_stats(ctx);
        printf("total packets sent: %d\n", stats->pkts_sent);
    }

    cleanup:
        close(fd);
        tcpreplay_close(ctx);
        return -1;
}