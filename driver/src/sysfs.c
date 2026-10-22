#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include "pci.h"
#include "registers.h"

/* Функция, вызываемая при записи в атрибут mask_reg */
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

/* Функция, вызываемая при чтении атрибута mask_reg */
static ssize_t mask_reg_show(struct device *dev, struct device_attribute *attr, char *buf){
    struct device_data* data = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%08x\n", ioread32(data->bar[BAR_AXI_LITE_IDX] + MASK_REG_OFS));
}


/* Функция, вызываемая при записи в атрибут detect_reg */
static ssize_t detect_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct device_data* data = dev_get_drvdata(dev);
    unsigned long flags;
    u32 val, reg;

    int err = kstrtouint(buf, 0, &val);
    if (err) {
        pr_alert("[FPGA] invalid sysfs input");
        return err;
    }

    if (val > DETECT_MODE_MAX){
        pr_alert("[FPGA] invalid sysfs input");
        return -EINVAL;
    }

    spin_lock_irqsave(&data->reg_lock, flags);
    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    reg &= ~DETECT_MODE_MASK;
    reg |= (val & DETECT_MODE_MASK);
    iowrite32(reg, data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);

    return count;
}

/* Функция, вызываемая при чтении атрибута detect_reg */
static ssize_t detect_reg_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct device_data *data = dev_get_drvdata(dev);
    unsigned long flags;
    u32 reg;

    spin_lock_irqsave(&data->reg_lock, flags);
    reg = ioread32(data->bar[BAR_AXI_LITE_IDX] + CONTROL_REG_OFS);
    spin_unlock_irqrestore(&data->reg_lock, flags);

    return sysfs_emit(buf, "%u\n", reg & DETECT_MODE_MASK);
}

/* Объявление атрибутов mask_reg и detect_reg для чтения и записи */
static DEVICE_ATTR_RW(mask_reg);
static DEVICE_ATTR_RW(detect_reg);

/* Массив описывает аттрибуты, которые будут входить в группу */
static struct attribute *fpga_attrs[] = {
    &dev_attr_mask_reg.attr,
    &dev_attr_detect_reg.attr,
    NULL,
};

/* Структура создаёт группу аттрибутов */
static const struct attribute_group fpga_group = {
    .name  = "fpga_sysfs",
    .attrs = fpga_attrs,
};

/* Массив групп для добавления через структуру pci_driver */
const struct attribute_group *fpga_groups[] = {
    &fpga_group,
    NULL,
};