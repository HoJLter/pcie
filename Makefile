obj-m += fpga_pcie.o

all:
	make -C /lib/modules/6.8.0-124-generic/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/6.8.0-124-generic/build M=$(shell pwd) clean
