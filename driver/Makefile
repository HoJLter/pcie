obj-m += fpga_pcie.o
fpga_pcie-objs := src/pci.o src/irq.o src/cdev.o

ccflags-y += -I$(src)/include

MODULE_NAME := fpga_pcie

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	insmod ./$(MODULE_NAME).ko
uninstall:
	rmmod $(MODULE_NAME)
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
	sudo echo 1 | sudo tee /sys/bus/pci/devices/0000:02:00.0/remove; \
	sudo echo 1 | sudo tee /sys/bus/pci/rescan; \

REMOTE_HOST := server1@45.81.255.24
REMOTE_DIR := ~/pcie
REPO := https://github.com/HoJLter/pcie.git

remote-reinstall:
	rsync -av --delete \
		--exclude='.git' \
		--exclude='*.o' \
		--exclude='*.ko' \
		--exclude='*.mod*' \
		--exclude='Module.symvers' \
		--exclude='modules.order' \
		-e "ssh -p 44070" \
		./ $(REMOTE_HOST):$(REMOTE_DIR)/

	ssh -t $(REMOTE_HOST) -p 44070 '\
		set -e; \
		sudo -v; \
		sudo dmesg -w & \
		DMESG_PID=$$!; \
		trap "sudo kill $$DMESG_PID 2>/dev/null || true" EXIT; \
		cd $(REMOTE_DIR); \
		make clean; \
		make; \
		sudo make uninstall; \
		sudo make install \
	'

remote-reconnect:
		ssh -t $(REMOTE_HOST) -p 44070 '\
		set -e; \
		sudo -v; \
		sudo echo 1 | sudo tee /sys/bus/pci/devices/0000:02:00.0/remove; \
		sudo echo 1 | sudo tee /sys/bus/pci/rescan; \
	'