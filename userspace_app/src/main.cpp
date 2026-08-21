#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>


#define DEVICE_TO_LISTEN "/dev/fpga"

int main(){
    std::cout << "started listening device" << DEVICE_TO_LISTEN << "\n";
    int fd = open(DEVICE_TO_LISTEN, 0);
    while (true){
        std::uint8_t irq_event = 0;
        read(fd, &irq_event, sizeof(irq_event));   
        if (1){
            std::cout << "interrupt received. Starting to play .pcap file.\n";
            int err = std::system("tcpreplay --intf1=enp8s0 data/traffic.pcap\n");
            if (err){
                std::cout << "TCPREPLAY ERROR: " << err << "\n";
                return err;
            }
        }
    }

    return 0;
}