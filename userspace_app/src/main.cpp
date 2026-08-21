#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>

int main(){
    int fd = open("/dev/fpga", 0);
    while (true){
        std::uint8_t irq_event = 0;
        read(fd, &irq_event, sizeof(irq_event));   
        if (irq_event){
            std::cout << "interrupt received. Starting to play .pcap file.";
            int err = std::system("tcpreplay --intf1=eth0 data/traffic.pcap");
            if (err){
                std::cout << "TCPREPLAY ERROR: " << err;
                return err;
            }
        }
    }

    return 0;
}