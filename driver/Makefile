obj-m += fpga_pcie.o
fpga_pcie-objs := src/pci.o src/irq.o src/cdev.o

ccflags-y += -I$(src)/include

MODULE_NAME := fpga_pcie

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	depmod -a
	modprobe $(MODULE_NAME)
uninstall:
	modprobe -r $(MODULE_NAME) || true
	depmod -a
reinstall:
	-make uninstall 
	make clean
	make all
	make install
	make clean

reinit:
	make uninstall
	make install

reconnect:
	sudo echo 1 | sudo tee /sys/bus/pci/devices/0000:04:00.0/remove; \
	sudo echo 1 | sudo tee /sys/bus/pci/rescan; \
