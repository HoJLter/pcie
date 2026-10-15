#include <tcpreplay/tcpreplay_api.h>

int main(void)
{
    printf("USERSPACE APP STARTED\n");

    tcpreplay_t* ctx = tcpreplay_init();
    if (ctx == NULL) {
        fprintf(stderr, "tcpreplay_init failed\n");
        return 1;
    }

    if (tcpreplay_set_interface(ctx, intf1, "enp0s3") < 0 ||
        tcpreplay_add_pcapfile(ctx, "data/traffic.pcap") < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0 ||
        tcpreplay_set_loop(ctx, 1) < 0 ||
        tcpreplay_prepare(ctx) < 0 ||
        tcpreplay_replay(ctx) < 0) {
        fprintf(stderr, "tcpreplay failed: %s\n", tcpreplay_geterr(ctx));
        tcpreplay_close(ctx);
        return 1;
    }

    const tcpreplay_stats_t* stats = tcpreplay_get_stats(ctx);
    printf("packets sent: " COUNTER_SPEC "\n", stats->pkts_sent);

    tcpreplay_close(ctx);
    return 0;
}