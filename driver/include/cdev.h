#ifndef CDEV_H
#define CDEV_H

#include <pci.h>

int fpga_init_chrdev(struct pci_dev* device);
void fpga_free_chrdev(struct pci_dev* device);

#endif