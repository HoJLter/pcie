DRIVER_NAME := fpga_pcie

DRIVER_DIR := driver
APP_DIR := userspace_app

.PHONY: all driver app load unload clean run logs test install-dep

all: driver app

unload:
	@echo "===> Выгрузка драйвера $(DRIVER_NAME) <==="
	-sudo rmmod $(DRIVER_NAME) 2>/dev/null || true

driver:
	@echo "===> Сборка драйвера <==="
	$(MAKE) -C $(DRIVER_DIR) all

load: driver unload
	@echo "===> Загрузка драйвера $(DRIVER_NAME) <==="
	sudo insmod $(DRIVER_DIR)/$(DRIVER_NAME).ko

clean:
	@echo "===> Очистка <==="
	$(MAKE) -C $(DRIVER_DIR) clean
	rm -rf $(APP_DIR)/build

app:
	@echo "===> Сборка userspace_app <==="
	$(MAKE) -C $(APP_DIR) comp

run:
	@echo "===> Сборка и запуск userspace_app <==="
	# Делегируем выполнение таргету run из userspace_app/Makefile
	$(MAKE) -C $(APP_DIR) run

logs:
	@echo "===> Логи ядра (Ctrl+C для выхода) <==="
	sudo dmesg -w


test: load run

install-dep:
	@echo "===> Установка зависимостей <==="
	$(MAKE) -C $(APP_DIR) install-dep

reginit:
	$(MAKE) -C $(DRIVER_DIR) reginit

regstatus:
	$(MAKE) -C $(DRIVER_DIR) regstatus

reconnect:
	$(MAKE) -C $(DRIVER_DIR) reconnect
