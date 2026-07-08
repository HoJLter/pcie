# PCIe FPGA Driver

Linux kernel driver for an FPGA connected via PCI Express.

## Features

* PCI device detection by Vendor ID and Device ID
* Device initialization
* BAR0 reservation
* MMIO mapping
* Driver load/unload support
* Foundation for implementing:

  * Register access
  * Interrupt handling (MSI/MSI-X)
  * DMA transfers
  * Character device interface

## Project Structure

```
.
├── fpga_driver.c      # PCIe driver source
├── Makefile           # Kernel module build script
├── README.md
└── .gitignore
```

## Requirements

* Linux kernel 6.x
* Linux kernel headers
* GCC
* Make

## Build

```bash
make
```

The resulting kernel module:

```
fpga_driver.ko
```

## Load Module

```bash
sudo insmod fpga_driver.ko
```

Verify that the driver has been loaded:

```bash
dmesg | tail
```

## Unload Module

```bash
sudo rmmod fpga_driver
```

## Device Detection

The driver registers itself as a PCI driver and automatically probes devices matching the specified Vendor ID and Device ID.

Example:

```c
#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x1234
```

## MMIO Access

BAR0 is reserved using:

```c
pci_request_region()
```

and mapped into kernel virtual memory using:

```c
pci_iomap()
```

Registers can then be accessed through MMIO:

```c
writel(value, bar0 + REG_CONTROL);

u32 status = readl(bar0 + REG_STATUS);
```

where each register is located at a fixed offset inside the BAR address space.

## Current Status

* [x] PCI driver registration
* [x] Device probing
* [x] BAR0 reservation
* [x] Device removal
* [x] MMIO register map
* [ ] Interrupt support (MSI)
* [ ] DMA support
* [ ] Character device
* [ ] User-space API

## License

This project is released under the MIT License.
