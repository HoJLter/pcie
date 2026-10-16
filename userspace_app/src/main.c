#include <tcpreplay/tcpreplay_api.h>


#define DEVICE_TO_LISTEN "/dev/fpga"
#define INTERFACE_TO_REPLAY "ens3"
#define DEVICE_PATH "/dev/fgpa"
#define PCAP_PATH "data/traffic.pcap"


int main(){
    printf("USERSPACE APP STARTED\n");
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0){
        printf("Error while opening file descriptor\n");
        return 1;
    }

    tcpreplay_t* ctx = tcpreplay_init();

    if (
        tcpreplay_set_interface(ctx, intf1, INTERFACE_TO_REPLAY) < 0 ||
        tcpreplay_add_pcapfile(ctx, PCAP_PATH) < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0
    ) {
        printf("setup failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }

    if (tcpreplay_prepare(ctx) < 0) {
        printf("prepare failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }   

    while (true){
        int err = read(fd, NULL, 0);
        if (err < 0){
            printf("Read error: %d\n", err);
            goto cleanup;
        }

        if (tcpreplay_set_loop(ctx, 1) < 0){
            printf("set loop failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        if (tcpreplay_replay(ctx) < 0) {
            printf("replay failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }
        
        const tcpreplay_stats_t *stats = tcpreplay_get_stats(ctx);
        printf("total packets sent: %d\n", stats->pkts_sent);
    }

    cleanup:
        close(fd);
        tcpreplay_close(ctx);
        return -1;
}