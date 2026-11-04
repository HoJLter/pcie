#ifndef SENDER_SOCKET_H
#define SENDER_SOCKET_H

#include "traffic_sender.h"

/* Функция, используемая для инициализации структуры traffic_sender */
int traffic_sender_init_socket(
    struct traffic_sender *sender,
    const char *interface_name,
    const char *pcap_path
);

#endif