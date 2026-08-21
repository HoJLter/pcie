#include <linux/irq.h>
#include "registers.h"
#include "pci.h"
#include <linux/fs.h>
#include <linux/cdev.h>


#define DEV_COUNT 1
#define BASEMINOR 0
#define DEV_NAME "fpga"


static struct cdev char_dev;
static dev_t dev_id;


struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = fpga_read,
    .poll = fpga_poll
};

int fpga_init_chrdev(){
    int err;
    
    err = alloc_chrdev_region(&dev_id, BASEMINOR, DEV_COUNT, DEV_NAME);
    if (err){
        pr_err("[FPGA] allocating chardev region fail");
        return err;
    }
    pr_info("[FPGA] allocating chardev region success");
    

    cdev_init(&char_dev, &fops);
    err = cdev_add(&char_dev, dev_id, DEV_COUNT);
    if (err){
        unregister_chrdev_region(dev_id, DEV_COUNT);
        pr_err("[FPGA] character device add fail");
        return err;
    }
    pr_info("[FPGA] character device add success");

    return 0
}

int fpga_free_chrdev(){
    cdev_del(&char_dev);
    unregister_chrdev_region(MAJOR(dev_id), DEV_NAME);
}


ssize_t fpga_read(struct file* f, char __user* user_buffer, size_t size, loff_t* offset){

}

ssize_t fpga_poll(struct file* f, struct poll_table_struct *wait){

}