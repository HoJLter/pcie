obj-m += fpga_pcie.o
fpga_pcie-objs := src/fpga_pcie.o

ccflags-y += -I$(shell pwd)/include

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
