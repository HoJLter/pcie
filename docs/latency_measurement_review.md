# Latency measurement: review, issues, and implementation plan

Status: design review. The changes below were applied in the working tree after
this review; see [latency_measurement_implementation_report.md](latency_measurement_implementation_report.md).
Nothing was built or run against hardware; the code is not tested code.

Scope: measure the time from the FPGA interrupt arriving at the driver to the
userspace application generating the first packet. The relevant paths are
[irq.c](../driver/src/irq.c), [cdev.c](../driver/src/cdev.c),
[pci.h](../driver/include/pci.h), [pci.c](../driver/src/pci.c), and
[main.c](../userspace_app/src/main.c). The Claude report
(`D:\Downloads\claude_report.md`) is used only as supplementary context; its
latency estimates are not measurements.

## Post-update recheck (commit `eea5028`)

The update is a naming-only commit, 5 insertions and 3 deletions across three
files. It does **not** touch `struct device_data`, `irq_handler`, `fpga_read`,
or any timestamp/concurrency logic.

| File | Change in `eea5028` |
|---|---|
| `driver/include/pci.h` | added `#define DEVICE_CLASS_NAME "fpga-class"` |
| `driver/src/cdev.c` | `DEV_NAME` changed from `"fpga"` to `"fpga0"`; added `SUBDEV_NAME "fpga_cdev_sub"`; `device_create(...)` now uses `SUBDEV_NAME` instead of `"fpga"` |
| `driver/src/pci.c` | `class_create("fpga")` changed to `class_create(DEVICE_CLASS_NAME)` |

### Earlier findings: still valid

| Earlier finding | Status after `eea5028` |
|---|---|
| `fpga_read()` returns one constant byte, ignores `size`; app passes `0` | unchanged |
| `bool is_irq` lost-wakeup race and interrupt coalescing | unchanged |
| No timestamp/sequence/event state in `device_data` | unchanged |
| `printk()` in the hot IRQ path | unchanged |
| IRQ requested/enabled before `wq`/locks are initialised | unchanged |
| `CONTROL_REG` RMW race between IRQ ack and sysfs | unchanged |
| Handler-entry timestamp excludes MSI/C-state delay | unchanged |
| `tcpreplay_replay()` return is not "on the wire" under TX_RING | unchanged |

The implementation plan in Sections 2–8 remained valid and has now been
applied; see the implementation report. The only naming adjustment was the
device-node path and the `DEV_NAME` comment in the snippets.

### New issue introduced by the update: device node path mismatch

`device_create()` receives `SUBDEV_NAME` (`"fpga_cdev_sub"`), so devtmpfs/udev
creates:

```text
/dev/fpga_cdev_sub
```

but the userspace application still opens:

```c
#define DEVICE_PATH "/dev/fpga"
```

`main.c` therefore fails at `open()` before the measurement loop starts. The
old `/dev/fpga` node may also still exist from a previous module load and point
at a stale major/minor, which makes the failure look like a driver bug.
**Implementation status:** fixed by setting `DEVICE_PATH` to
`/dev/fpga_cdev_sub`; see the implementation report.

Fix one side, not both:

- **Preferred for compatibility:** set `SUBDEV_NAME` back to `"fpga"` (and keep
  `DEV_NAME` as the region label), or
- **Preferred if the new name is intentional:** change `DEVICE_PATH` to
  `"/dev/fpga_cdev_sub"` and update any udev rules/documentation.

`DEV_NAME` (`"fpga0"`) only labels the char region in `/proc/devices`; it does
not create the `/dev` node. `DEVICE_TO_LISTEN` in `main.c` is unused and still
contains the stale `/dev/fpga` path.

The class rename to `fpga-class` changes the class path under
`/sys/class/fpga-class/`, but the Makefile's `SYSFS_PATH` uses the PCI device
path (`/sys/bus/pci/devices/0000:00:04.0/fpga_sysfs`), so `make reginit` is not
affected. Custom udev rules matching the old class name `fpga` would need
updating; none are in this repository.

## Key conclusions

1. The proposed three steps are directionally correct but not sufficient: they
   do not define event identity, handle overwritten interrupts, or define the
   clock/endpoint of "packet generation".
2. `ktime_get_ns()` at the first line of the hardirq is the right source and
   placement. `jiffies` is far too coarse and realtime can jump.
3. The current `bool is_irq` has a lost-wakeup race and collapses multiple
   interrupts. Replace it with a sequence number plus a single event slot (or a
   `kfifo` if every interrupt must be preserved).
4. There is an existing crash window: `probe()` enables interrupts in
   `fpga_init_irq()` before `fpga_init_chrdev()` initialises the waitqueue. New
   spinlocks make this worse; initialise all synchronisation in `probe()` first.
5. `read()` must return a versioned fixed-size record, not a bare timestamp.
   The current handler returns one byte and ignores `size`; the app passes
   `size == 0`.
6. Handler entry is **after** the MSI transit, CPU interrupt entry and C-state
   exit. The driver-only number therefore undercounts the true FPGA-to-packet
   delay, potentially by tens to 150 µs.
7. With tcpreplay's default `PACKET_TX_RING` path, `tcpreplay_replay()`
   returning does not mean the packet is on the wire. Label the metric
   accordingly or use a synchronous/TX-timestamped path.
8. Commit `eea5028` renamed the created device node to `/dev/fpga_cdev_sub`
   while the app still opened `/dev/fpga`. The implementation resolves this by
   updating `DEVICE_PATH` to `/dev/fpga_cdev_sub`.

---

## 1. What the existing code actually does

| Path | Current behaviour | Consequence for latency measurement |
|---|---|---|
| [irq_handler](../driver/src/irq.c#L17-L27) | `printk()` first, then `data->is_irq = true`, `wake_up_interruptible()`, then an MMIO read-modify-write ack | No timestamp at all. The `printk` can add microseconds to milliseconds and is before any measurement point. |
| [fpga_read](../driver/src/cdev.c#L26-L45) | Waits on `data->is_irq`, clears the flag, copies **one byte `0x01`**, returns `1` | It does not return "nothing": it returns a constant byte. It ignores `size`, so `read(fd, buf, 0)` still writes one byte. It cannot identify an interrupt or carry a timestamp. |
| [struct device_data](../driver/include/pci.h#L26-L36) | `bar[]`, `irq_number`, `cdev`, `dev_id`, `wq`, `bool is_irq` | No timestamp, no sequence number, no lock. |
| [userspace loop](../userspace_app/src/main.c#L36-L57) | `read(fd, &val, 0)`, ignores the returned value, then replays the pcap | `val` is only partly written and unused. The call passes count `0`, which is an invalid way to fetch a record. |
| [probe](../driver/src/pci.c#L27-L80) | `fpga_init_irq()` (request IRQ and enable FPGA interrupts) is called **before** `fpga_init_chrdev()`, which initialises `wq` | An interrupt can fire before `wq` (and, after this change, the spinlocks) are initialised. This is a real crash window, not just a measurement issue. |
| [sysfs detect_reg_store](../driver/src/sysfs.c#L28-L50) | Read-modify-writes `CONTROL_REG` without a lock | Races with `fpga_irq_ack()`'s read-modify-write on the same register: one update can be lost. |

So the premise "the `read` handler currently returns nothing" should be
corrected: it returns a one-byte constant. That byte is not a timestamp and
cannot support a reliable measurement.

---

## 2. Assessment of the proposed three-step approach

### Step 1 — record a timestamp in the interrupt handler

**Direction is right, but the details matter.**

- Timestamp source. Do **not** use `jiffies`:
  - `jiffies` is updated once per tick. At `HZ=250` the granularity is 4 ms;
    at `HZ=1000` it is 1 ms. The target latencies are tens of microseconds, so
    jiffies is not a timestamp at all for this purpose.
  - `jiffies` is also tick-aligned, so differences quantise to tick boundaries
    and hide the very jitter you want to measure.
- Do **not** use realtime (`ktime_get_real_ns()`, `gettimeofday()`,
  `CLOCK_REALTIME`). NTP steps, `settimeofday()`, leap-second handling, and
  manual clock changes can move it backwards; an elapsed time can be negative.
- Use `ktime_get_ns()` (`CLOCK_MONOTONIC`). It is the same time base as
  userspace `clock_gettime(CLOCK_MONOTONIC)`, has nanosecond resolution, and
  does not jump. `ktime_get_mono_fast_ns()` is an alternative that avoids the
  timekeeper seqcount (NMI-safe, slightly cheaper), but it is not NTP-adjusted;
  if you use it, compute all differences in the kernel and do not compare it
  directly against userspace `CLOCK_MONOTONIC`.
- Capture the timestamp at the **first line** of the handler, before `printk`,
  before the ack's MMIO access, and before `wake_up_interruptible()`. This is
  the closest point the driver can observe, but it is already **after** the MSI
  TLP transit, the CPU's interrupt-entry sequence, and any C-state exit. Those
  can cost tens to 150 µs and are **not** in the measured interval. The FPGA
  edge itself is not observable by the driver unless the FPGA latches a
  hardware counter that the driver can read.
- Record the timestamp before the ack. The ack is a non-posted MMIO read plus a
  write over PCIe; it can cost on the order of a microsecond and must not be
  included in the "handler entry" timestamp.

### Step 2 — return the timestamp from `read`

**Right idea, but a bare 64-bit value is not a sufficient ABI.**

A single `u64` lacks:

- a way to detect that another interrupt overwrote the one being read;
- a version/size for future extension;
- a kernel-side service timestamp (so the user can separate kernel wake-up
  latency from application time);
- a defined size contract.

Return a fixed-size, versioned struct instead (Section 3.2). Also fix the
current `size` handling:

- `size < sizeof(struct fpga_irq_event)` (including `0`) should return
  `-EINVAL`, not copy a partial record; the application must pass
  `sizeof(struct fpga_irq_event)`. This also makes an old binary fail loudly
  instead of spinning on a zero-length read.
- `size >= sizeof(...)` should copy exactly the struct and return its size.
- `-EINTR`/`-ERESTARTSYS` and `O_NONBLOCK`/`-EAGAIN` need to be handled.

On current kernels, `vfs_read()` does not short-circuit a zero count before
calling the driver's `->read`; the old app's `read(fd, &val, 0)` really does
enter `fpga_read` and receives one byte. The new driver returns `-EINVAL` for a
zero-length read, so the app must pass `sizeof(struct fpga_irq_event)`.

A sysfs attribute for "last timestamp" was considered and rejected: it needs a
second syscall, can race with the event, and does not couple the wake-up with
the value. `read()` returning one event record is the natural interface.

### Step 3 — compute elapsed time in userspace

**Correct only if the clock domains and the endpoint are defined.**

- Use `clock_gettime(CLOCK_MONOTONIC, ...)`, not `CLOCK_REALTIME`.
- `clock_gettime(CLOCK_MONOTONIC_RAW)` is also wrong here because the kernel
  uses `CLOCK_MONOTONIC`; the two drift apart by NTP slew.
- Compare absolute timestamps only when both sides are in the same time
  namespace. `CLOCK_MONOTONIC` is virtualised by time namespaces (Linux 5.6+):
  an application in a container can see a different `CLOCK_MONOTONIC` offset
  than the initial namespace, while `ktime_get_ns()` in the driver uses the
  global monotonic clock. Subtracting them then gives a huge, meaningless
  number. The robust formula is:
  `total = kernel_latency_ns + (t_after_send - t_after_read)`, because each
  difference is taken inside one clock domain. The driver returns
  `latency_ns` precisely for this purpose. Alternatively, compare
  `t_after_read` with the driver's `read_ts_ns` and fall back if the skew is
  large.
- "Packet generation" is ambiguous. With tcpreplay 4.6.1's default
  `PACKET_TX_RING` path, `tcpreplay_replay()` returns after the frame is copied
  into the TX ring, not after it is sent; the Claude report estimates a further
  0–160 µs (about 80 µs average) before the ring thread calls `sendto()`.
  Therefore a timestamp taken immediately after `tcpreplay_replay()` measures
  "interrupt → frame queued in the TX ring", not "interrupt → first packet on
  the wire". This must be stated in the metric name. See Section 5 for options.

### Cross-cutting issue: concurrency and multiple interrupts

The current `bool is_irq` has two distinct failure modes:

1. **Lost wake-up.** `read()` evaluates `data->is_irq == false` and is about to
   sleep. The interrupt handler sets it to `true` and calls
   `wake_up_interruptible()` before the reader is actually queued. The reader
   then sleeps, and the wake-up is lost until the next interrupt. In the
   meantime the application is blocked even though an event occurred.
2. **Lost event.** `read()` sees `is_irq == true`, then a second interrupt sets
   it to `true` again before `read()` executes `data->is_irq = false`. The
   second interrupt is silently merged into the first; one `read()` returns for
   two events.

`is_irq` is also a plain `bool` written from hardirq context and read from
process context without `READ_ONCE()` or a lock. That is a data race (KCSAN
will flag it), even if it happens to work on x86.

If two threads read the same file descriptor, both can observe `is_irq`, one
clears it, and the other may sleep or return a duplicate. There is no event
identity, so userspace cannot tell which interrupt it received.

The fix used in this plan is a lock-protected sequence number plus one event
slot:

- the handler increments `irq_seq` and publishes `{seq, irq_ts}` under
  `event_lock`;
- `read()` waits for `irq_seq != consumed_seq`, snapshots the slot, and sets
  `consumed_seq = seq` under the same lock;
- `open()` sets `consumed_seq = irq_seq` to discard history;
- `missed` exposes overwritten interrupts.

`wait_event_interruptible()` rechecks its condition after the waiter is queued,
so the lost-wake-up interleaving above is closed. The spinlock makes the event
and sequence snapshot consistent. If every interrupt must be measured rather
than only the latest, use a `kfifo` (Section 6).

A separate init-order bug makes this worse: `fpga_init_irq()` requests the IRQ
and enables FPGA interrupts before `fpga_init_chrdev()` calls
`init_waitqueue_head()`. An interrupt in that window calls
`wake_up_interruptible()` on an uninitialised waitqueue (and would take
uninitialised spinlocks after this change). Initialise the waitqueue and all
locks in `probe()` before `fpga_init_irq()`; see Section 3.6.

### Summary of the assessment

| Proposed step | Verdict | Main correction |
|---|---|---|
| Timestamp in IRQ handler | Keep | `ktime_get_ns()` at handler entry, before printk/ack/wake; remove printk. |
| Return it via `read` | Keep, redesign ABI | Versioned event struct with `seq`, `missed`, `read_ts_ns`, `latency_ns`; validate `size`. |
| Compute in userspace | Keep, define endpoint | `CLOCK_MONOTONIC`; handle time namespaces; do not call the replay-return time "on the wire". |

The proposed approach misses the two hardest parts: **event identity under
concurrent/overwritten interrupts**, and **the exact endpoint of the
measurement**.

---

## 3. Recommended design

### 3.1 Data flow

```
FPGA event
  -> MSI
  -> irq_handler: t_irq = ktime_get_ns()          (handler entry)
       publish {seq, t_irq} under event_lock
       ack
       wake_up_interruptible()
  -> read(): wait for seq != consumed
       snapshot event, t_read = ktime_get_ns()    (read service)
       latency = t_read - t_irq
       copy struct to userspace
  -> userspace: t_read_user = clock_gettime(MONOTONIC)
       tcpreplay_replay()
       t_done = clock_gettime(MONOTONIC)
       total = t_done - t_irq   (same namespace)
             ≈ latency + (t_done - t_read_user)  (namespace-safe)
```

Only one event slot is kept. This is deliberate: for latency measurement, an
interrupt that has already been overwritten is stale; returning the newest one
is more useful than queueing stale events. `seq` and `missed` make the
overwrite visible. If every interrupt must be preserved, use a `kfifo` of
events instead (Section 6).

### 3.2 Userspace ABI: `driver/include/fpga_uapi.h` (new file)

```c
#ifndef FPGA_UAPI_H
#define FPGA_UAPI_H

#include <linux/types.h>

#define FPGA_EVENT_ABI_VERSION 1u

/**
 * struct fpga_irq_event - one interrupt observation returned by read(2)
 *
 * @abi_version: FPGA_EVENT_ABI_VERSION
 * @size:        sizeof(struct fpga_irq_event); lets userspace validate layout
 * @seq:         interrupt sequence number (32-bit, wraps)
 * @missed:      interrupts overwritten since the previous successful read
 * @irq_ts_ns:   CLOCK_MONOTONIC nanoseconds at hardirq handler entry
 * @read_ts_ns:  CLOCK_MONOTONIC nanoseconds when read() claimed the event
 * @latency_ns:  read_ts_ns - irq_ts_ns, computed in the kernel
 *
 * All timestamps use the kernel's CLOCK_MONOTONIC (ktime_get_ns()).
 * The layout is fixed-width, pointer-free and has no padding; the fields are
 * laid out naturally on both 32-bit and 64-bit Linux.
 */
struct fpga_irq_event {
    __u32 abi_version;
    __u32 size;
    __u32 seq;
    __u32 missed;
    __u64 irq_ts_ns;
    __u64 read_ts_ns;
    __u64 latency_ns;
};

#endif /* FPGA_UAPI_H */
```

Layout (checked by a compile-time assertion in the driver and the app):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `abi_version` |
| 4 | 4 | `size` |
| 8 | 4 | `seq` |
| 12 | 4 | `missed` |
| 16 | 8 | `irq_ts_ns` |
| 24 | 8 | `read_ts_ns` |
| 32 | 8 | `latency_ns` |
| **40** | | **total** |

The struct is 40 bytes on both i386 and x86-64. `read()` must return exactly 40
bytes on success, or a negative errno; it never returns a partial record.

### 3.3 `driver/include/pci.h`

Add the includes and replace the `is_irq` flag with the event state. Keep the
existing fields.

```c
#ifndef PCI_H
#define PCI_H

#include <linux/io.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "fpga_uapi.h"

/* ... existing defines ... */

/**
 * struct device_data - per-device driver state
 *
 * @event_lock:   protects @event, @irq_seq and @consumed_seq
 * @event:        latest interrupt observation, published by the hardirq
 * @irq_seq:      monotonic 32-bit counter, incremented for every interrupt
 * @consumed_seq: sequence number of the last event handed to userspace
 * @reg_lock:     protects CONTROL_REG read-modify-write (IRQ ack vs sysfs)
 * @open_lock:    serialises open/release and protects @opened
 * @opened:       single-consumer policy for /dev/fpga_cdev_sub
 */
struct device_data {
    void __iomem *bar[XDMA_BAR_CNT];

    int irq_number;

    struct cdev char_dev;
    dev_t dev_id;

    wait_queue_head_t wq;

    spinlock_t event_lock;
    struct fpga_irq_event event;
    u32 irq_seq;
    u32 consumed_seq;

    spinlock_t reg_lock;

    struct mutex open_lock;
    bool opened;
};

/* ... global_drv_data unchanged ... */

#endif
```

`is_irq` is removed. `event` is zeroed by `devm_kzalloc()`; the spinlocks,
mutex and waitqueue must be initialised before the IRQ is requested (Section
3.6).

### 3.4 `driver/src/irq.c`

Remove the unconditional `printk`. Publish the event under the lock, ack, then
wake.

```c
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/ktime.h>
#include "irq.h"
#include "pci.h"
#include "registers.h"
#include <linux/wait.h>

/* Confirm interrupt processing. Serialised against the sysfs RMW. */
static void fpga_irq_ack(struct device_data *data)
{
    unsigned long flags;
    u32 val;

    spin_lock_irqsave(&data->reg_lock, flags);
    val = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    val |= HANDLED_ACK_MASK;
    iowrite32(val, data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);
}

static irqreturn_t irq_handler(int irq, void *inp_data)
{
    struct device_data *data = inp_data;
    unsigned long flags;
    u64 irq_ts_ns;

    (void)irq;

    /*
     * The measurement starts here. This is the first driver instruction that
     * observes the interrupt; do not put printk, MMIO or anything expensive
     * before it.
     */
    irq_ts_ns = ktime_get_ns();

    spin_lock_irqsave(&data->event_lock, flags);
    data->irq_seq++;
    data->event.irq_ts_ns = irq_ts_ns;
    data->event.read_ts_ns = 0;
    data->event.latency_ns = 0;
    data->event.seq = data->irq_seq;
    spin_unlock_irqrestore(&data->event_lock, flags);

    fpga_irq_ack(data);
    wake_up_interruptible(&data->wq);

    return IRQ_HANDLED;
}
```

Notes:

- `pr_debug()` may remain only if dynamic debug is guaranteed off; for
  production measurements delete the print entirely. A `printk` without a
  level defaults to `KERN_WARNING` and can reach a serial console.
- Ack before wake makes the device ready before userspace can run; if the
  absolute minimum latency is the goal, swap these two lines, but then the
  measured interval can overlap the ack. Document which order you use.
- `missed` is computed in `read()` as `event.seq - consumed_seq - 1` (modulo
  2^32), so it counts events overwritten since the last successfully delivered
  record. With `consumed_seq` set to `irq_seq` at open, the first interrupt has
  `missed == 0`. Computing it at read time (rather than at publish time) avoids
  mis-counting an interrupt that arrives while `copy_to_user()` is running.
- `irq` is unused; `(void)irq;` if the compiler warns.

### 3.5 `driver/src/cdev.c`

Complete replacement for the current `open`/`read`, plus a `release`:

```c
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>
#include "registers.h"
#include "pci.h"
#include "cdev.h"

/* ... DEV_COUNT, BASEMINOR, DEV_NAME "fpga0", SUBDEV_NAME "fpga_cdev_sub" ... */

extern struct global_drv_data drv_data;

static int fpga_open(struct inode *inode, struct file *f)
{
    struct device_data *data =
        container_of(inode->i_cdev, struct device_data, char_dev);
    unsigned long flags;

    if (mutex_lock_interruptible(&data->open_lock))
        return -ERESTARTSYS;

    if (data->opened) {
        mutex_unlock(&data->open_lock);
        return -EBUSY;
    }

    /*
     * Discard interrupts that arrived before open(). The first read() then
     * reports a fresh interrupt, not an arbitrarily old one.
     */
    spin_lock_irqsave(&data->event_lock, flags);
    data->consumed_seq = data->irq_seq;
    spin_unlock_irqrestore(&data->event_lock, flags);

    data->opened = true;
    f->private_data = data;
    mutex_unlock(&data->open_lock);

    pr_debug("[FPGA] open\n");
    return 0;
}

static int fpga_release(struct inode *inode, struct file *f)
{
    struct device_data *data = f->private_data;

    (void)inode;
    mutex_lock(&data->open_lock);
    data->opened = false;
    mutex_unlock(&data->open_lock);

    return 0;
}

static ssize_t fpga_read(struct file *f, char __user *user_buffer,
                         size_t size, loff_t *offset)
{
    struct device_data *data = f->private_data;
    struct fpga_irq_event ev;
    unsigned long flags;
    bool pending;
    int ret;

    (void)offset;

    if (size < sizeof(ev))
        return -EINVAL;          /* no partial or zero-length ABI record */

    for (;;) {
        spin_lock_irqsave(&data->event_lock, flags);
        pending = (data->irq_seq != data->consumed_seq);
        if (pending) {
            ev = data->event;
            ev.abi_version = FPGA_EVENT_ABI_VERSION;
            ev.size = (__u32)sizeof(ev);
            /*
             * Compute missed at read time. consumed_seq still points at the
             * last delivered event, so this counts exactly the events that
             * were overwritten since then.
             */
            ev.missed = ev.seq - data->consumed_seq - 1u;
            ev.read_ts_ns = ktime_get_ns();
            if (ev.read_ts_ns >= ev.irq_ts_ns)
                ev.latency_ns = ev.read_ts_ns - ev.irq_ts_ns;
            else
                ev.latency_ns = 0;   /* defensive: cross-CPU clock skew */
            data->consumed_seq = ev.seq;
        }
        spin_unlock_irqrestore(&data->event_lock, flags);

        if (pending)
            break;

        if (f->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(data->wq,
                    READ_ONCE(data->irq_seq) != READ_ONCE(data->consumed_seq));
        if (ret)
            return ret;          /* -ERESTARTSYS -> EINTR in userspace */
    }

    if (copy_to_user(user_buffer, &ev, sizeof(ev)))
        return -EFAULT;

    return sizeof(ev);
}

static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = fpga_read,
    .open    = fpga_open,
    .release = fpga_release,
};
```

`fpga_read()` claims the event (sets `consumed_seq`) while holding the lock,
before `copy_to_user()`. This serialises two threads reading the same file
descriptor, so an event cannot be delivered twice. The trade-off is that an
`-EFAULT` loses that one event; that is acceptable because `-EFAULT` means a
broken userspace buffer, and the next interrupt is normally the useful sample.
If retry-on-EFAULT is required, hold a per-file mutex across the read and
commit `consumed_seq` only after a successful copy.

`fpga_open()` resets `consumed_seq` to the current `irq_seq`, so an interrupt
that happened while the device was closed is not reported as a fresh sample.
The single-consumer `-EBUSY` policy is deliberate; if multiple consumers are
needed, replace the single slot with a `kfifo` and remove the exclusivity.

In `fpga_init_chrdev()`:

- remove `init_waitqueue_head(&data->wq);` (it moves to probe, Section 3.6);
- add `BUILD_BUG_ON(sizeof(struct fpga_irq_event) != 40);` near the top;
- keep the rest unchanged.

`fpga_free_chrdev()` is unchanged. A blocked reader on an unplugged device is a
separate lifecycle issue (Section 6).

### 3.6 `driver/src/pci.c`: initialise synchronisation before the IRQ

This is required: `fpga_init_irq()` requests the IRQ and enables FPGA
interrupts, while `fpga_init_chrdev()` currently calls
`init_waitqueue_head()` afterwards. An interrupt in that window calls
`wake_up_interruptible()` on an uninitialised waitqueue and, with the new code,
takes uninitialised spinlocks. Move the initialisation into `probe()`:

```c
    data = devm_kzalloc(&device->dev, sizeof(*data), GFP_KERNEL);
    if (!data) {
        pr_err("[FPGA] devm_kzalloc failed\n");
        return -ENOMEM;
    }
    pci_set_drvdata(device, data);

    /*
     * Initialise everything the hardirq touches before fpga_init_irq()
     * requests the IRQ and enables interrupts on the card.
     */
    spin_lock_init(&data->event_lock);
    spin_lock_init(&data->reg_lock);
    mutex_init(&data->open_lock);
    init_waitqueue_head(&data->wq);

    /* ... pcim_enable_device, pci_set_master, iomap ... */

    err = fpga_init_irq(device);
    if (err)
        return err;

    err = fpga_init_chrdev(device);
    if (err)
        return err;
```

Do not re-initialise the waitqueue in `fpga_init_chrdev()`; re-initialising a
live waitqueue would drop waiters.

### 3.7 `driver/src/sysfs.c`: protect the shared `CONTROL_REG` RMW

`fpga_irq_ack()` and `detect_reg_store()` both do read-modify-write on the same
register. Use the same `reg_lock`:

```c
static ssize_t detect_reg_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct device_data *data = dev_get_drvdata(dev);
    unsigned long flags;
    u32 val, reg;
    int err = kstrtouint(buf, 0, &val);

    if (err)
        return err;
    if (val > DETECT_MODE_MAX)
        return -EINVAL;

    spin_lock_irqsave(&data->reg_lock, flags);
    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    reg &= ~DETECT_MODE_MASK;
    reg |= (val & DETECT_MODE_MASK);
    iowrite32(reg, data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);

    return count;
}

static ssize_t detect_reg_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct device_data *data = dev_get_drvdata(dev);
    unsigned long flags;
    u32 reg;

    spin_lock_irqsave(&data->reg_lock, flags);
    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);

    return sysfs_emit(buf, "%u\n", reg & DETECT_MODE_MASK);
}
```

`mask_reg_store()` is a plain write to a different register and does not need
the lock.

### 3.8 Userspace: `userspace_app/src/main.c`

Add the ABI include and measure around the replay. The application must pass
`sizeof(ev)`, not `0`.

```c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tcpreplay/tcpreplay_api.h>

#include "fpga_uapi.h"

_Static_assert(sizeof(struct fpga_irq_event) == 40, "fpga ABI size");

#define DEVICE_PATH         "/dev/fpga_cdev_sub"
#define INTERFACE_TO_REPLAY "ens3"
#define PCAP_PATH           "data/traffic.pcap"

static uint64_t now_ns(clockid_t clk)
{
    struct timespec ts;

    if (clock_gettime(clk, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    struct fpga_irq_event ev;
    tcpreplay_t *ctx;
    int fd = -1;

    ctx = tcpreplay_init();
    if (!ctx) {
        fprintf(stderr, "tcpreplay_init failed\n");
        return 1;
    }

    if (tcpreplay_set_interface(ctx, intf1, INTERFACE_TO_REPLAY) < 0 ||
        tcpreplay_add_pcapfile(ctx, PCAP_PATH) < 0 ||
        tcpreplay_set_speed_mode(ctx, speed_topspeed) < 0) {
        fprintf(stderr, "setup failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }

    if (tcpreplay_prepare(ctx) < 0) {
        fprintf(stderr, "prepare failed: %s\n", tcpreplay_geterr(ctx));
        goto cleanup;
    }

    /*
     * Open only after the potentially slow tcpreplay setup. fpga_open()
     * discards events that happened before the open, so the first read()
     * cannot return an interrupt that arrived while the pcap/interface was
     * being prepared.
     */
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open " DEVICE_PATH);
        goto cleanup;
    }

    for (;;) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        uint64_t t_read, t_done, total_ns;
        int64_t skew_ns, abs_ns;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read " DEVICE_PATH);
            goto cleanup;
        }
        if (n != (ssize_t)sizeof(ev)) {
            fprintf(stderr, "short read: %zd (expected %zu)\n",
                    n, sizeof(ev));
            goto cleanup;
        }
        if (ev.abi_version != FPGA_EVENT_ABI_VERSION ||
            ev.size != sizeof(ev)) {
            fprintf(stderr, "ABI mismatch: version=%u size=%u\n",
                    ev.abi_version, ev.size);
            goto cleanup;
        }

        t_read = now_ns(CLOCK_MONOTONIC);

        if (tcpreplay_set_loop(ctx, 1) < 0) {
            fprintf(stderr, "set loop failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }
        if (tcpreplay_replay(ctx) < 0) {
            fprintf(stderr, "replay failed: %s\n", tcpreplay_geterr(ctx));
            goto cleanup;
        }

        t_done = now_ns(CLOCK_MONOTONIC);

        /*
         * Same clock domain (initial time namespace): the absolute difference
         * includes everything, including copy_to_user and syscall return.
         * If the app runs in a time namespace, the absolute difference is
         * bogus; fall back to kernel latency + user-side delta.
         */
        skew_ns = (int64_t)t_read - (int64_t)ev.read_ts_ns;
        abs_ns = (int64_t)t_done - (int64_t)ev.irq_ts_ns;
        if (llabs(skew_ns) < 1000000 && abs_ns >= 0) { /* same domain */
            total_ns = (uint64_t)abs_ns;
        } else {
            total_ns = ev.latency_ns + (t_done - t_read);
        }

        printf("seq=%" PRIu32 " missed=%" PRIu32
               " kernel=%" PRIu64 " us"
               " post_read=%" PRIu64 " us"
               " total=%" PRIu64 " us\n",
               ev.seq, ev.missed,
               (uint64_t)(ev.latency_ns / 1000),
               (uint64_t)((t_done - t_read) / 1000),
               total_ns / 1000);
    }

cleanup:
    if (fd >= 0)
        close(fd);
    tcpreplay_close(ctx);
    return 1;
}
```

`tcpreplay_set_interface(ctx, intf1, ...)` is valid: `intf1` is an enum in
`tcpreplay_api.h` ([header](https://git.in-ulm.de/cbiedl/tcpreplay/raw/6a56c9bdfd46f08dacc080fd2eb463dddf2a819e/src/tcpreplay_api.h)):
`typedef enum { intf1 = 1, intf2 } tcpreplay_intf`. The existing line is not a
compile error.

Opening `/dev/fpga_cdev_sub` after `tcpreplay_prepare()` (as above) matters: the current
program opens it before the tcpreplay setup, so an interrupt during that setup
can be pending on the first `read()` and produce a meaningless first sample.
`fpga_open()` resets `consumed_seq`, so opening after the setup discards those
events. If you must open earlier, drain pending events with `O_NONBLOCK` before
the measurement loop.

If you instead restore `SUBDEV_NAME` to `"fpga"` in `cdev.c`, change
`DEVICE_PATH` back to `"/dev/fpga"`. The two names must match; at the time of
the `eea5028` review they were out of sync, and the implementation fixed this by
using `/dev/fpga_cdev_sub`.

CMake include path (or add `-I../driver/include` if compiling directly):

```cmake
target_include_directories(app PRIVATE
    ${TCPREPLAY_STATIC_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../driver/include)
```

For production measurement, do not `printf()` inside the hot loop. Store
samples in an array and print min/median/p99/max after N iterations, otherwise
stdout/console time perturbs the next iteration.

---

## 4. Metrics, formulas, and what they mean

Let:

- `t_irq` = `ev.irq_ts_ns` — handler entry.
- `t_read` = `ev.read_ts_ns` — read claims the event (before `copy_to_user`).
- `t_user` = userspace timestamp immediately after `read()` returns.
- `t_done` = userspace timestamp after `tcpreplay_replay()` returns.

Then:

| Metric | Formula | What it includes | What it excludes |
|---|---|---|---|
| Kernel wake-up latency | `ev.latency_ns = t_read - t_irq` | handler entry, event publish, ack, wake-up, scheduler, read entry | `copy_to_user`, syscall return, application work |
| Post-read application time | `t_done - t_user` | syscall return, `set_loop`, pcap open/parse, tcpreplay setup, TX-ring enqueue | kernel wake-up, actual TX-ring flush/wire |
| Total to replay return | `t_done - t_irq` (same namespace) | everything through the replay call | actual wire transmission under TX_RING |
| Namespace-safe total | `ev.latency_ns + (t_done - t_user)` | kernel + application, each in its own domain | the small `t_read`→`t_user` syscall-return gap |

Report all four, or at least label the one you use. Do not present
`t_done - t_irq` as "interrupt to wire" while tcpreplay uses `PACKET_TX_RING`.

### Clock-domain check

At `t_user`, `skew_ns = t_user - ev.read_ts_ns` is normally a few hundred
nanoseconds to a few microseconds (syscall return). If it is hours or days, the
application is in a different time namespace; use the namespace-safe total.
This is a pragmatic check, not a proof; the clean fix is to always use
`ev.latency_ns + (t_done - t_user)`.

Do not compare `ev.irq_ts_ns` against `CLOCK_MONOTONIC_RAW` or
`CLOCK_BOOTTIME`.

---

## 5. The "packet generation" endpoint

This is the biggest semantic risk in the whole feature.

According to the Claude report, tcpreplay 4.6.1 on Linux normally uses
`PACKET_TX_RING`; `sendpacket()` only copies the frame into the ring and a
separate thread flushes it with `sendto()`. If that is true for the build on the
target (verify with `strace -f -tt`, or check the build's `config.h` for
`HAVE_TX_RING`), then:

- `tcpreplay_replay()` returning is **not** the first packet on the wire;
- the measured `total` is an "enqueue" latency, smaller and less variable than
  the real end-to-end delay;
- the difference can be tens to hundreds of microseconds and is exactly the
  jitter the report attributes to the TX-ring thread.

Options, in increasing order of fidelity:

1. **State the metric honestly.** Call it "interrupt → replay return" and do
   not claim wire latency. This is sufficient if the goal is driver/wake-up
   instrumentation.
2. **Use synchronous send.** Build tcpreplay with
   `CFLAGS=-DFORCE_INJECT_PF_PACKET` (the report notes that
   `--enable-force-pf` has an upstream name mismatch). Then `replay` returns
   after `sendto()`, i.e. after the frame is queued to the kernel stack. Still
   not the wire, but much closer.
3. **Timestamp the actual send path.** Patch `sendpacket()`/`txring_put()` in
   libtcpreplay to call `clock_gettime(CLOCK_MONOTONIC)` at the handoff, or
   expose a callback. This is the only application-side way to timestamp the
   real "packet generated" event under TX_RING.
4. **Use kernel/NIC TX timestamps.** `SO_TIMESTAMPING` with
   `SOF_TIMESTAMPING_TX_SOFTWARE` or `SOF_TIMESTAMPING_TX_HARDWARE` on a socket
   you own; this requires replacing or patching tcpreplay's socket handling.
   Hardware TX timestamps are the closest to "on the wire".
5. **External capture.** A tap/oscilloscope/FPGA GPIO gives the physical
   reference, but it is a different measurement setup.

Also note: if the pcap contains more than one packet, `t_done` after
`tcpreplay_replay()` includes all of them. Use a one-packet pcap for
"first packet" latency, or hook the first `sendpacket()` call (option 3).

The driver cannot timestamp packet generation; it only knows interrupt and
read. Do not add a write/ioctl "I sent it" timestamp from userspace and call it
packet time — it measures a later syscall.

---

## 6. Multiple interrupts, multiple readers, and lifecycle

### Multiple interrupts

The single-slot design reports the **latest** interrupt and counts overwritten
ones in `missed`:

- `missed == 0`: the sample is the next interrupt after the previous read.
- `missed > 0`: `missed` interrupts were overwritten; only the latest is
  measured. The application is not keeping up; the older samples are stale
  anyway.

If you need one record per interrupt, replace the slot with a `kfifo`:

- `kfifo_alloc(&data->events, 64, GFP_KERNEL)` at probe, `kfifo_free()` at
  remove.
- In the handler, `kfifo_in()` a small `{seq, irq_ts}` record; if `kfifo_in()`
  returns less than a full record, increment a lost counter.
- In `read()`, wait for `!kfifo_is_empty()`, then `kfifo_to_user()`.
- `kfifo` is safe for one producer (the hardirq) and one consumer (read).
- Queued records include queueing delay, so a backlog makes the measured
  latency include time the event spent waiting; for latency work the latest
  slot is usually better.

### Multiple readers

The proposed code enforces one open (`-EBUSY`). This is appropriate for a
single board and a single measurement app. If multiple consumers are required,
remove the exclusivity and use either a `kfifo` (each reader gets distinct
records) or per-file `last_seq` state. With the global `consumed_seq` and no
exclusivity, two apps can reset each other's view at open.

### Sequence wrap

`seq` is 32-bit. At 1 million interrupts/second it wraps in about 71 minutes.
The `irq_seq != consumed_seq` test is wrap-safe as long as the app reads at
least once per 2^32 interrupts (otherwise an exact wrap can look like "no
event"). If that is a concern, make `seq` 64-bit and protect the wait condition
with the lock (`wait_event_interruptible_lock_irq`), or use a separate
`atomic_t pending` flag for the condition.

### Init/remove lifecycle

- **Init order is a bug** (Section 3.6): with the current order, an interrupt
  can call `wake_up_interruptible()` on an uninitialised waitqueue. Move the
  waitqueue and lock initialisation before `fpga_init_irq()`.
- `remove()` calls `pci_free_irq_vectors()` before the devres-managed IRQ is
  freed and before `fpga_free_chrdev()`. Consider:
  `devm_free_irq(&device->dev, data->irq_number, data);` before
  `pci_free_irq_vectors(device)`, and remove the char device first.
- A process blocked in `read()` is not woken when the device is removed; it can
  block forever or touch freed `device_data`. Stop the app before `rmmod`, or
  add a `removing` flag, `wake_up_all()`, and `-ENODEV` handling. This is
  outside the latency feature but matters for a robust test harness.

---

## 7. Exact file/function change list

| File | Function/area | Change |
|---|---|---|
| `driver/include/fpga_uapi.h` | new | `struct fpga_irq_event`, `FPGA_EVENT_ABI_VERSION` |
| `driver/include/pci.h` | `struct device_data` | add `event_lock`, `event`, `irq_seq`, `consumed_seq`, `reg_lock`, `open_lock`, `opened`; remove `is_irq`; include `ktime.h`, `spinlock.h`, `mutex.h`, `fpga_uapi.h` |
| `driver/src/irq.c` | `irq_handler` | `ktime_get_ns()` first; publish event under lock; remove `printk` |
| `driver/src/irq.c` | `fpga_irq_ack` | `reg_lock` around the `CONTROL_REG` RMW |
| `driver/src/cdev.c` | `fpga_read` | wait on `seq`; snapshot; `read_ts_ns`/`latency_ns`; copy struct; `EINVAL`/`EAGAIN`/`EFAULT`/`EINTR` |
| `driver/src/cdev.c` | `fpga_open`, `fpga_release`, `fops` | single-consumer open; reset `consumed_seq`; add `.release` |
| `driver/src/cdev.c` | `fpga_init_chrdev` | remove `init_waitqueue_head`; add ABI `BUILD_BUG_ON` |
| `driver/src/pci.c` | `probe` | initialise `wq`, `event_lock`, `reg_lock`, `open_lock` before `fpga_init_irq` |
| `driver/src/sysfs.c` | `detect_reg_store`, `detect_reg_show` | `reg_lock` around the `CONTROL_REG` RMW |
| `userspace_app/src/main.c` | main loop | pass `sizeof(ev)`; validate ABI; timestamps; totals; no `printf` in hot loop |
| `userspace_app/src/main.c` | `DEVICE_PATH` | use `/dev/fpga_cdev_sub` (current `SUBDEV_NAME`), or restore `SUBDEV_NAME` to `"fpga"` and keep `/dev/fpga` |
| `userspace_app/CMakeLists.txt` | include dirs | add `../driver/include` |
| `driver/Makefile` | — | no change; `-I$(src)/include` already covers the new header |

No change is needed to the PCI IDs, BAR mapping, or sysfs attribute group name
(`fpga_sysfs`). The class path is now `/sys/class/fpga-class/`; the Makefile's
`SYSFS_PATH` uses the PCI device path and is unaffected.

---

## 8. Measurement procedure and validation

1. **Build and load** the driver, confirm `/dev/fpga_cdev_sub` exists, configure
   `mask_reg`/`detect_reg` as in the Makefile's `reginit` target.
2. **Check the no-event path**: `read(fd, buf, 0)` returns `-EINVAL`;
   `read(fd, buf, 1)` returns `-EINVAL`; a blocking read blocks; a second open
   returns `-EBUSY`.
3. **Trigger one interrupt**: read returns 40 bytes, `seq` increments,
   `missed == 0`, `latency_ns` is plausible for handler-entry → read (tens of
   microseconds). It will not include a C-state exit that happened before the
   handler was entered; compare with `power:cpu_idle`/`irq_handler_entry` traces
   if you need the full CPU-side delay.
4. **Trigger a burst** before reading: the next read returns the latest `seq`
   with `missed > 0`. Confirm no crash and no lost wake-up.
5. **Compare with ftrace**: enable `irq:irq_handler_entry` and
   `irq:irq_handler_exit`; `ev.irq_ts_ns` should match the entry timestamp
   within the trace overhead (roughly a microsecond). `ev.latency_ns` should
   match `entry -> read wake-up` plus scheduling.
6. **Compare with `cyclictest`** on the same CPU/affinity to separate driver
   latency from general scheduler wake-up latency.
7. **Run with and without** `SCHED_FIFO`, `mlockall`, CPU pinning, and
   `/dev/cpu_dma_latency = 0`. Record the configuration with every result;
   otherwise the numbers are not comparable.
8. **Collect at least 1000 samples**, discard the first 100 (cold code, page
   faults, first pcap open), and report min/p50/p90/p99/max plus a histogram.
   Report `missed` and the interrupt rate alongside.
9. **Check the endpoint**: `strace -f -tt` around a trigger, and `config.h` for
   `HAVE_TX_RING`, to confirm whether the first `sendto()` happens in the
   replay thread or later. Label the metric accordingly.
10. **32-bit check** if the app is 32-bit: `sizeof(struct fpga_irq_event)` must
    be 40 and the offsets must match; `read()` copies fixed-width fields, so no
    compat ioctl is needed.

---

## 9. Limitations to document

- **Handler entry is not the FPGA edge.** The measurement starts after the MSI
  TLP transit, the CPU's interrupt-entry sequence and any C-state exit. Those
  are **not** included; if a deep C-state costs 20–150 µs, the reported
  latency is smaller than the true FPGA-to-packet delay. It also excludes any
  FPGA pipeline delay before the MSI is raised. Do not compare these numbers
  directly with the Claude report's end-to-end estimate.
- **`ktime_get_ns()` resolution** depends on the clocksource (TSC is
  nanosecond-scale; HPET/ACPI PMT is coarser). On virtualised hosts, cross-vCPU
  TSC skew can produce small negative differences; the code clamps
  `latency_ns` to zero, but the absolute total can still be slightly off.
- **Time namespaces** break absolute kernel-vs-user subtraction; use the
  namespace-safe sum or the skew check.
- **PREEMPT_RT** may run the handler in a threaded context; the handler-entry
  timestamp then includes thread wake-up, which is a different metric. State
  the kernel configuration.
- **Instrumentation overhead** is small (a `ktime_get_ns`, a spinlock, a
  struct write, a wake-up that already existed), but the removed `printk` was
  the dominant perturbation. Do not add tracing to the hot path while
  collecting.
- **Single slot** means bursts are collapsed; `missed` makes that visible.
- **TX_RING** means the application-side endpoint is enqueue, not wire. See
  Section 5.
- **No hardware test was performed here.** The estimates in the Claude report
  are static analysis, not measurements.

---

## 10. Suggested implementation order

1. Fix the init ordering in `probe()` (waitqueue/locks before
   `fpga_init_irq()`), independent of the feature.
2. Add `fpga_uapi.h` and the `device_data` fields.
3. Change `irq_handler` (timestamp + publish, remove `printk`).
4. Change `fpga_read`/`open`/`release` and add `reg_lock` to `fpga_irq_ack`.
5. Update `main.c` and CMake.
6. Remove `pr_info` from the read path and any remaining per-IRQ logging.
7. Run the validation matrix in Section 8.
8. Only then consider optimisations (synchronous send, preload, SCHED_FIFO,
   busy-poll/`mmap`), and re-measure with the same instrumentation.
