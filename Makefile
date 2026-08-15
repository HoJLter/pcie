obj-m += fpga_pcie.o
fpga_pcie-objs := src/fpga_pcie.o

ccflags-y += -I$(src)/include

MODULE_NAME := fpga_pcie

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	depmod -a
	echo "$(MODULE_NAME)" > /etc/modules-load.d/$(MODULE_NAME).conf
	modprobe $(MODULE_NAME)

uninstall:
	rmmod $(MODULE_NAME) || true
	rm -f /etc/modules-load.d/$(MODULE_NAME).conf
	rm -f /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a

reinstall:
	make clean
	make all
	make uninstall
	make install

reinit:
	make uninstall
	make install

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
		sudo dmesg -w & \
		DMESG_PID=$$!; \
		trap "sudo kill $$DMESG_PID 2>/dev/null || true" EXIT; \
		cd $(REMOTE_DIR); \
		make clean; \
		make; \
		sudo make uninstall; \
		sudo make install \
	'