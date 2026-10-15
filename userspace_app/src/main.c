#include <tcpreplay/tcpreplay_api.h>


#define DEVICE_TO_LISTEN "/dev/fpga"
//#define INTERFACE_TO_REPLAY "ens3"
#define INTERFACE_TO_REPLAY "enp0s3"


int main(){
    printf("USERSPACE APP STARTED\n");
    int err;
    // int fd = open("/dev/fpga", O_RDONLY);
    // if (fd < 0){
    //     printf("Error while opening file descriptor\n");
    //     return 1;
    // }


    while (true){
        tcpreplay_t* ctx = tcpreplay_init();

        if (
            tcpreplay_set_interface(ctx, intf1, INTERFACE_TO_REPLAY) < 0 ||
            tcpreplay_add_pcapfile(ctx, "data/traffic.pcap") < 0 ||
            tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0 ||
            tcpreplay_set_loop(ctx, 1) < 0
        ) {
            fprintf(stderr, "setup failed: %s\n", tcpreplay_geterr(ctx));
            tcpreplay_close(ctx);
            return 1;
        }

        err = read(fd, 0, 0);
        if (err < 0){
            printf("Read error: %d\n", err);
            return 1;
        }

        if (tcpreplay_prepare(ctx) < 0) {
            fprintf(stderr, "prepare failed: %s\n", tcpreplay_geterr(ctx));
            tcpreplay_close(ctx);
            return 1;
        }   

        if (tcpreplay_replay(ctx) < 0) {
            fprintf(stderr, "replay failed: %s\n", tcpreplay_geterr(ctx));
            tcpreplay_close(ctx);
            return 1;
        }

        const tcpreplay_stats_t *stats = tcpreplay_get_stats(ctx);
        printf("packets sent: " COUNTER_SPEC "\n", stats->pkts_sent);

        tcpreplay_close(ctx);

        sleep(1);
    }
}