#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>


#define DEVICE_TO_LISTEN "/dev/fpga"
#define INTERFACE_TO_REPLAY "ens3"

int main(){
    std::cout << "started listening device" << DEVICE_TO_LISTEN << "\n";
    int fd = open(DEVICE_TO_LISTEN, O_RDONLY);
    if (fd < 0){
        perror("open /dev/fpga"); return 1;
        return -1;
    }
    while (true){
        std::uint8_t irq_event = 0;
        ssize_t n = read(fd, &irq_event, sizeof(irq_event));   
        if (n < 0){
            perror("read"); break;
            return -1;
        }
        std::cout << "interrupt received. Starting to play .pcap file.\n";
        int err = std::system("tcpreplay --intf1="INTERFACE_TO_REPLAY" ../data/traffic.pcap");
        if (err){
            std::cout << "TCPREPLAY ERROR: " << err << "\n";
            return err;
        }
    }
    return 0;
}