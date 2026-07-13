obj-m += fpga_pcie.o
fpga_pcie-objs := src/fpga_pcie.o

ccflags-y += -I$(shell pwd)/include

MODULE_NAME := fpga_pcie

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	depmod -a
	echo "$(MODULE_NAME)" > /etc/modules-load.d/$(MODULE_NAME).conf

uninstall:
	rm -f /etc/modules-load.d/$(MODULE_NAME).conf
	rm -f /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a
