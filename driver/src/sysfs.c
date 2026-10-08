#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include "pci.h"

static ssize_t mask_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct device_data* data = dev_get_drvdata(dev);
}

static ssize_t mask_reg_show(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct device_data* data = dev_get_drvdata(dev);
}