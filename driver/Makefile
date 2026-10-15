obj-m += fpga_pcie.o
fpga_pcie-objs := src/pci.o src/irq.o src/cdev.o src/sysfs.o

ccflags-y += -I$(src)/include

MODULE_NAME := fpga_pcie
SYSFS_PATH := /sys/bus/pci/devices/0000:00:04.0/fpga_sysfs

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	sudo make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	sudo depmod -a
	sudo modprobe $(MODULE_NAME)
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

reginit:
	echo "Mask reg init: "
	echo 3 | sudo tee ${SYSFS_PATH}/mask_reg 
	cat ${SYSFS_PATH}/mask_reg
	echo "Detect reg init: "
	echo 1 | sudo tee ${SYSFS_PATH}/detect_reg
	cat ${SYSFS_PATH}/detect_reg

regstatus:
	cat ${SYSFS_PATH}/detect_reg
	cat ${SYSFS_PATH}/mask_reg

reconnect:
	sudo echo 1 | sudo tee /sys/bus/pci/devices/0000:04:00.0/remove; \
	sudo echo 1 | sudo tee /sys/bus/pci/rescan; \
