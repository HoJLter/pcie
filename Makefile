DRIVER_NAME := fpga_pcie
DRIVER_DIR  := driver
APP_DIR     := userspace_app

.PHONY: all unload driver load clean-driver app run logs clean test install-dep reginit regstatus reconnect

all: unload driver load clean-driver app run

unload:
	@echo "===> Проверка и выгрузка драйвера $(DRIVER_NAME) <==="
	@if lsmod | grep -q "^$(DRIVER_NAME)"; then \
		echo "Драйвер $(DRIVER_NAME) найден, выгружаем..."; \
		sudo rmmod $(DRIVER_NAME); \
		echo "Драйвер $(DRIVER_NAME) успешно выгружен."; \
	else \
		echo "Драйвер $(DRIVER_NAME) не загружен, пропускаем."; \
	fi

driver:
	@echo "===> Сборка драйвера $(DRIVER_NAME) <==="
	$(MAKE) -C $(DRIVER_DIR) all

load:
	@if [ ! -f $(DRIVER_DIR)/$(DRIVER_NAME).ko ]; then \
		echo "Файл $(DRIVER_DIR)/$(DRIVER_NAME).ko не найден, собираем..."; \
		$(MAKE) driver; \
	fi
	@echo "===> Загрузка драйвера $(DRIVER_NAME) <==="
	sudo insmod $(DRIVER_DIR)/$(DRIVER_NAME).ko
	@echo "Драйвер $(DRIVER_NAME) успешно загружен."

clean-driver:
	@echo "===> Очистка мусора после сборки драйвера <==="
	$(MAKE) -C $(DRIVER_DIR) clean

app:
	@echo "===> Сборка userspace_app <==="
	$(MAKE) -C $(APP_DIR) comp

run:
	@if [ ! -f $(APP_DIR)/build/app ]; then \
		echo "Приложение $(APP_DIR)/build/app не найдено, собираем..."; \
		$(MAKE) app; \
	fi
	@echo "===> Запуск userspace_app с правами sudo <==="
	cd $(APP_DIR) && sudo ./build/app

logs:
	@echo "===> Логи ядра FPGA (Ctrl+C для выхода) <==="
	sudo dmesg -wH | grep --line-buffered -i -E "\[FPGA\]|$(DRIVER_NAME)"

clean:
	@echo "===> Очистка всех артефактов <==="
	$(MAKE) -C $(DRIVER_DIR) clean
	rm -rf $(APP_DIR)/build

install-dep:
	@echo "===> Установка зависимостей <==="
	$(MAKE) -C $(APP_DIR) install-dep

reginit:
	$(MAKE) -C $(DRIVER_DIR) reginit

regstatus:
	$(MAKE) -C $(DRIVER_DIR) regstatus

reconnect:
	$(MAKE) -C $(DRIVER_DIR) reconnect
