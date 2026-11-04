#define _GNU_SOURCE

#include <errno.h>    
#include <fcntl.h>    
#include <sched.h>    
#include <stdint.h>   
#include <stdio.h>   
#include <string.h> 
#include <sys/mman.h> 
#include <unistd.h>

#include "optimization.h"

/* Оптимизация запуска */
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
        fprintf(stderr, "Ошибка при установке SCHED_FIFO: %s\n", strerror(errno));
        return 1;
    }

    printf("Политика SCHED_FIFO с приоритетом %d успешно применена!\n", param.sched_priority);
    return 0;
}