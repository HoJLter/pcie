#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include "pci.h"
#include "registers.h"

static ssize_t mask_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct device_data* data = dev_get_drvdata(dev);
    u32 mask;
    int err = kstrtouint(buf, 0, &mask);
    if (err){
        pr_alert("[FPGA] Invalid sysfs input!");
        return err; 
    }
    iowrite32(mask, data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS);
    return count;
}

static ssize_t mask_reg_show(struct device *dev, struct device_attribute *attr, char *buf){
    struct device_data* data = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%08x\n", ioread32(data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS));
}



static ssize_t detect_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct device_data* data = dev_get_drvdata(dev);
    u32 val;

    int err = kstrtouint(buf, 0, &val);
    if (err) {
        pr_alert("[FPGA] invalid sysfs input");
        return err;
    }

    if (val > DETECT_MODE_MASK){
        pr_alert("[FPGA] invalid sysfs input");
        return -EINVAL;
    }

    u32 reg;
    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS);
    reg &= ~DETECT_MODE_MASK;
    reg |= val & DETECT_MODE_MASK;
    iowrite32(reg, data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS);
    return count;
}

static ssize_t detect_reg_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct device_data *data = dev_get_drvdata(dev);
    u32 reg, val;


    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS);
    val = reg & DETECT_MODE_MASK;

    return sysfs_emit(buf, "%u\n", val);
}