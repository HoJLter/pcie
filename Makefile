obj-m += fpga_pcie.o

all:
	make -C /lib/modules/7.0.0-27-generic/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/7.0.0-27-generic/build M=$(shell pwd) clean