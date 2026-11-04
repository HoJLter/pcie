#ifndef SENDER_TCPREPLAY_H
#define SENDER_TCPREPLAY_H

#include "traffic_sender.h"

/* Функция, используемая для инициализации структуры traffic_sender */
int traffic_sender_init_tcpreplay(
    struct traffic_sender *sender,
    const char *interface_name,
    const char *pcap_path
);

#endif