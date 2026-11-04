#define _GNU_SOURCE

#include "traffic_sender.h"
#include "sender_tcpreplay.h"
#include <tcpreplay/tcpreplay_api.h>


void preload_pcap_file(tcpreplay_t *ctx, int idx); // без этого tcpreplay будет открывать
// и читать файл traffic.pcap с диска каждый раз, когда приходит прерывание.

/* Функция отправки пакетов */
int tcpreplay_send(void* ctx){
    if (tcpreplay_set_loop(ctx, 1) < 0) {
        printf("set loop failed: %s\n", tcpreplay_geterr(ctx));
        return -1;
    }

    if (tcpreplay_replay(ctx) < 0) {
        printf("replay failed: %s\n", tcpreplay_geterr(ctx));
        return -1;
    }

    return 0;
}

/* Функция освобождения ресурсов */
int tcpreplay_destroy(void* ctx){
    tcpreplay_close(ctx);
    return 0;
}

/* Функция, используемая для инициализации структуры traffic_sender */
int traffic_sender_init_tcpreplay(
    struct traffic_sender *sender,
    const char *interface_name,
    const char *pcap_path
){
    tcpreplay_t *ctx = tcpreplay_init();
    sender->ctx = ctx;

    // Включаем предзагрузку ДО prepare
    tcpreplay_set_preload_pcap(ctx, true);

    if (tcpreplay_set_interface(ctx, intf1, interface_name) < 0 || tcpreplay_add_pcapfile(ctx, pcap_path) < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0) {
        printf("setup failed: %s\n", tcpreplay_geterr(ctx));
        return -1;
    }
    printf("SETUP PASSED\n");

    if (tcpreplay_prepare(ctx) < 0) {
        printf("prepare failed: %s\n", tcpreplay_geterr(ctx));
        return -1;
    }
    printf("PREPARE PASSED\n");

    // Принудительно грузим pcap в память
    preload_pcap_file(ctx, 0);

    sender->destroy=tcpreplay_destroy;
    sender->send=tcpreplay_send;
}



