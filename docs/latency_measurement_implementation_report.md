# Отчёт о применении изменений: измерение задержки IRQ → пакет

## Исходное состояние

Работа выполнена поверх `eea5028` (`Update: minor names`). Изменения находятся
в рабочем дереве и не закоммичены. Дизайн-ревью и обоснование решений —
в [latency_measurement_review.md](latency_measurement_review.md).

Сборка драйвера и userspace-приложения в текущем окружении не выполнялась:
здесь нет Linux-ядра/заголовков и libtcpreplay. Проверка была статической:
чтение кода, сверка ABI (40 байт), проверка сигнатур и путей.

## Что изменено

### 1. Новый userspace ABI: `driver/include/fpga_uapi.h`

Добавлен заголовок с фиксированной структурой события:

```c
struct fpga_irq_event {
    __u32 abi_version;   /* FPGA_EVENT_ABI_VERSION */
    __u32 size;          /* sizeof(struct fpga_irq_event) == 40 */
    __u32 seq;           /* номер прерывания */
    __u32 missed;        /* сколько событий перезаписано */
    __u64 irq_ts_ns;     /* CLOCK_MONOTONIC на входе в обработчик */
    __u64 read_ts_ns;    /* CLOCK_MONOTONIC при выдаче события */
    __u64 latency_ns;    /* read_ts_ns - irq_ts_ns */
};
```

Заголовок общий для ядра и userspace. В драйвере размер проверяется через
`BUILD_BUG_ON`, в приложении — через `_Static_assert`.

### 2. `driver/include/pci.h`

В `struct device_data` добавлены:

- `event_lock` — защищает событие и счётчики;
- `event` — последнее событие;
- `irq_seq` — счётчик прерываний;
- `consumed_seq` — номер последнего выданного события;
- `reg_lock` — защита read-modify-write `CONTROL_REG`;
- `open_lock` и `opened` — политика одного потребителя.

Флаг `is_irq` удалён. Добавлены include-файлы `ktime.h`, `spinlock.h`,
`mutex.h`, `types.h` и `fpga_uapi.h`.

### 3. `driver/src/irq.c`

- `ktime_get_ns()` вызывается первой строкой обработчика, до `printk`, MMIO и
  `wake_up_interruptible()`.
- Вместо `is_irq = true` обработчик под `event_lock` увеличивает `irq_seq` и
  публикует `irq_ts_ns`/`seq`.
- `printk()` из горячего пути удалён.
- `fpga_irq_ack()` выполняет read-modify-write `CONTROL_REG` под `reg_lock`,
  чтобы не терять обновления из sysfs.
- При ошибке `devm_request_irq()` теперь освобождаются IRQ-векторы.

Порядок в обработчике: timestamp → публикация события → ack → wake_up.

### 4. `driver/src/cdev.c`

- `fpga_open()`:
  - берёт `open_lock` и запрещает второй одновременный `open` (`-EBUSY`);
  - под `event_lock` выставляет `consumed_seq = irq_seq`, отбрасывая события,
    пришедшие до открытия.
- `fpga_release()` снимает `opened`.
- `fpga_read()`:
  - требует `size >= sizeof(struct fpga_irq_event)`, иначе `-EINVAL`;
  - ждёт `irq_seq != consumed_seq` через `wait_event_interruptible()`;
  - под `event_lock` снимает событие, считает `missed`, `read_ts_ns` и
    `latency_ns`, выставляет `consumed_seq`;
  - поддерживает `O_NONBLOCK` (`-EAGAIN`) и `-ERESTARTSYS`;
  - копирует ровно 40 байт через `copy_to_user()`.
- `fops` стал `const`, добавлен `.release`.
- `init_waitqueue_head()` убран из `fpga_init_chrdev()`: очередь инициализируется
  раньше, в `probe()`.
- Добавлен `BUILD_BUG_ON(sizeof(struct fpga_irq_event) != 40)`.
- Исправлены cleanup-пути: при `device_create()` failure и при NULL
  `device_class` выполняется `cdev_del()`/`unregister_chrdev_region()`.
- Убран лишний `pr_info` из `open()`/`read()`.

### 5. `driver/src/pci.c`

- `wq`, `event_lock`, `reg_lock` и `open_lock` инициализируются сразу после
  `pci_set_drvdata()`, **до** `fpga_init_irq()`. Это закрывает окно, в котором
  прерывание могло прийти до инициализации waitqueue/спинлоков.
- `remove()` переупорядочен: сначала удаляется cdev, затем освобождается IRQ
  (`devm_free_irq`), затем векторы и bus-master.

### 6. `driver/src/sysfs.c`

`detect_reg_store()` и `detect_reg_show()` работают с `CONTROL_REG` под
`reg_lock`. Это устраняет гонку с `fpga_irq_ack()`: ack и sysfs больше не
теряют биты друг друга.

### 7. `userspace_app/src/main.c`

- `DEVICE_PATH` изменён на `/dev/fpga_cdev_sub` — это имя сейчас создаёт
  `device_create(..., SUBDEV_NAME)`.
- Неиспользуемый `DEVICE_TO_LISTEN` удалён.
- Устройство открывается **после** `tcpreplay_prepare()`, чтобы не получить
  первым событием прерывание, случившееся во время подготовки pcap/интерфейса.
- `read()` вызывается с `sizeof(ev)`, проверяются `n`, `abi_version` и `size`.
- Замер:
  - `t_read` сразу после `read()`;
  - `tcpreplay_replay()`;
  - `t_done` сразу после replay;
  - `total = t_done - irq_ts_ns`, если приложение и драйвер в одном time
    namespace;
  - иначе `total = latency_ns + (t_done - t_read)`.
- Для `CLOCK_MONOTONIC` используется vDSO `clock_gettime()`.
- Выводится строка `seq/missed/kernel/post_read/total` в микросекундах.

### 8. `userspace_app/CMakeLists.txt`

Добавлен include-путь `../driver/include`, чтобы приложение использовало тот же
`fpga_uapi.h`, что и драйвер.

## Принятые решения и отличия от дизайн-ревью

1. **Имя устройства.** Оставлено текущее имя из `eea5028`
   (`SUBDEV_NAME "fpga_cdev_sub"`), а приложение переведено на
   `/dev/fpga_cdev_sub`. Альтернатива — вернуть `SUBDEV_NAME "fpga"` и
   `/dev/fpga`; выбран вариант без отката последнего переименования.
2. **Один потребитель.** `open()` возвращает `-EBUSY` при втором открытии.
   Это упрощает семантику события. Если понадобятся несколько потребителей,
   нужен `kfifo` или per-file состояние.
3. **Одно событие, а не очередь.** Для измерения задержки важно самое свежее
   прерывание; перезаписанные события видны через `missed`. `kfifo` не
   реализован.
4. **Событие помечается выданным до `copy_to_user()`.** Это не даёт двум
   потокам одного fd получить одно событие дважды. Цена — при `-EFAULT`
   теряется одно событие; это допустимо для диагностического ABI.
5. **Печать в цикле.** Строка выводится после `t_done`, поэтому не входит в
   текущий замер, но влияет на следующий цикл. Для точных серий лучше писать
   выборку в файл/буфер; это отмечено в коде.

## Что не делалось

- Драйвер не собирался и не проверялся на реальной FPGA: нет Linux-среды и
  оборудования.
- Не реализованы `poll()`, `mmap()`-буфер, `kfifo`, `SCHED_FIFO`, `mlockall`,
  `/dev/cpu_dma_latency`.
- Не исправлена ситуация, когда процесс заблокирован в `read()` в момент
  удаления устройства: `cdev_del()` не будит его, а `device_data` освобождается
  через devres. Для тестового стенда приложение нужно останавливать до `rmmod`.
- Не менялась логика `tcpreplay`/TX_RING. Поэтому `total` — это
  «прерывание → возврат из `tcpreplay_replay()`», а не гарантированно
  «прерывание → пакет на проводе». Для последнего нужен синхронный `send()`
  или TX-timestamp.

## Ограничения измерения

- `irq_ts_ns` — вход в обработчик, а не фронт на FPGA. Время MSI-транзита,
  входа в прерывание и выхода из C-state **не входит** в измерение; это может
  давать недооценку на десятки–сотни микросекунд.
- Время до `read()` включает планировщик и wake-up, что и является целью
  измерения kernel latency.
- Абсолютная разность `t_done - irq_ts_ns` корректна только в одном time
  namespace; иначе используется сумма `latency_ns + (t_done - t_read)`.
- При переполнении 32-битного `seq` (≈71 минута на 1 млн IRQ/с) есть
  теоретическая коллизия, если приложение не читает событие за 2^32 прерываний.

## Как собрать и проверить

На Linux-машине с заголовками текущего ядра и установленным libtcpreplay:

```bash
# драйвер
cd driver
make
sudo make install          # modprobe fpga_pcie
ls -l /dev/fpga_cdev_sub
dmesg | tail -50

# sysfs, если нужен BDF 0000:00:04.0
make reginit

# приложение
cd ../userspace_app
make comp
sudo ./build/app
```

Ожидаемый вывод приложения:

```text
SETUP PASSED
PREPARE PASSED
seq=1 missed=0 kernel=... us post_read=... us total=... us
```

Что проверить вручную:

1. `read(fd, buf, 0)` и `read(fd, buf, 1)` возвращают `-EINVAL`.
2. Второй `open("/dev/fpga_cdev_sub")` возвращает `-EBUSY`.
3. Одиночное прерывание: `seq` растёт, `missed == 0`, `latency_ns` ненулевой.
4. Серия прерываний без чтения: следующее чтение возвращает последнее событие
   с `missed > 0`.
5. `ftrace` (`irq:irq_handler_entry`) и `strace -f -tt` помогают сверить
   `irq_ts_ns` и момент `sendto()`.
6. `dmesg` не должен печатать строку на каждое прерывание: `printk` из
   обработчика удалён.

## Изменённые файлы

| Файл | Что сделано |
|---|---|
| `driver/include/fpga_uapi.h` | новый ABI-заголовок |
| `driver/include/pci.h` | состояние события, блокировки, удалён `is_irq` |
| `driver/src/irq.c` | timestamp, публикация события, `reg_lock`, удалён `printk` |
| `driver/src/cdev.c` | новый `read`/`open`/`release`, ABI, cleanup |
| `driver/src/pci.c` | инициализация до IRQ, порядок `remove` |
| `driver/src/sysfs.c` | `reg_lock` вокруг `CONTROL_REG` |
| `userspace_app/src/main.c` | новый ABI, тайминги, путь устройства |
| `userspace_app/CMakeLists.txt` | include-путь к `fpga_uapi.h` |

## Следующие шаги

1. Собрать драйвер на целевой машине; устранить возможные замечания компилятора
   (особенно по поводу `BUILD_BUG_ON`, `READ_ONCE`, `struct device`).
2. Собрать приложение с libtcpreplay; проверить `_Static_assert` и путь
   `/dev/fpga_cdev_sub`.
3. Прогнать проверки из раздела «Как собрать и проверить».
4. Для измерения «до провода» собрать tcpreplay с синхронным `send()` или
   добавить TX-timestamp; текущий `total` — это задержка до возврата replay.
