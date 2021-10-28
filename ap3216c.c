#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/types.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "ap3216c_reg.h"

#define DEVICE_CNT      1
#define DEVICE_NAME     "ap3216c"

struct ap3216c_device {
        int devid;
        int major;
        int minor;
        struct cdev ap3216c_cdev;
        struct class *class;
        struct device *device;
        struct i2c_client *client;
        unsigned int ir, als, ps;
};
static struct ap3216c_device ap3216c_dev;

static int ap3216c_open(struct inode *inode, struct file *filp);
static ssize_t ap3216c_read(struct file *filp,
                        char __user *user,
                        size_t count,
                        loff_t *loffp);
static int ap3216c_release(struct inode *inode, struct file *filp);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = ap3216c_open,
        .read = ap3216c_read,
        .release = ap3216c_release,
};

static int ap3216c_read_regs(struct i2c_client *client,
                             u8 reg,
                             void *buf,
                             unsigned short int len)
{
        struct i2c_msg msgs[2];

        /* msgs[0]:第一条写信息，发送要读取的寄存器首地址 */
        msgs[0].addr = client->addr;    /* I2C器件地址   */
        msgs[0].flags = 0;              /* 标记为发送数据 */
        msgs[0].len = 1;                /* reg长度      */
        msgs[0].buf = &reg;             /* 读取的首地址  */

        /* msgs[1]:第二条读信息，读取寄存器数据 */
        msgs[1].addr = client->addr;    /* I2C器件地址   */
        msgs[1].flags = I2C_M_RD;       /* 标记为读取数据 */
        msgs[1].len = len;              /* 读取数据长度   */
        msgs[1].buf = buf;              /* 读取数据缓冲区 */

        return i2c_transfer(client->adapter, msgs, 2);
}

static int ap3216c_write_regs(struct i2c_client *client,
                              u8 reg,
                              u8 *buf,
                              u16 len)
{
        struct i2c_msg msg;
        u8 temp_buf[512] = {0};

        if (len >= 512)
                len = 511;

        temp_buf[0] = reg;
        memcpy(&temp_buf[1], buf, len);

        msg.addr = client->addr;        /* I2C slave addr */
        msg.flags = 0;                  /* set flag is write */
        msg.len = len + 1;              /* set data len */
        msg.buf = temp_buf;             /* set buffer data */

        return i2c_transfer(client->adapter, &msg, 1);
}

static int ap3216c_read_single_reg(struct ap3216c_device *dev, u8 reg)
{
        int ret = 0;
        int data = 0;
        ret = ap3216c_read_regs(dev->client, reg, &data, 1);
        if (ret < 0) {
                printk("ap3216c read regs error!\n");
                return ret;
        }
        return data;
}

static int ap3216c_write_single_reg(struct ap3216c_device *dev, u8 reg,u8 data)
{
        int ret = 0;
        ret = ap3216c_write_regs(dev->client, reg, &data, 1);
        if (ret < 0)
                printk("ap3216c write error!\n");

        return ret;
}

void ap3216c_readdata(struct ap3216c_device *dev)
{
        unsigned char i = 0;
        unsigned char buf[6] = {0};

#if 1
        for (i = 0; i < 6; i++) {
                buf[i] = ap3216c_read_single_reg(dev, AP3216C_IR_DATA_L + i);
        }
#else
        /* 测试发现此方法读取出来的数据不对 */
        ap3216c_read_regs(dev->client, 0x0A, &buf[0], 6);
#endif
        if (buf[0] & 0x80) {
                dev->ir = 0;
        } else {
                dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0x03);
        }

        dev->als = ((unsigned short)buf[3] << 8) | buf[2];

        if (buf[4] & 0x40)
                dev->ps = 0;
        else
                dev->ps = ((unsigned short)(buf[5] & 0x3f) << 4) | (buf[4] & 0x0f);
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
        int ret = 0;
        filp->private_data = &ap3216c_dev;

        /* AP3216C模块初始化 */
        /* Reset AP3216C */
        ap3216c_write_single_reg(&ap3216c_dev, AP3216C_SYS_CONFIG, 0x04);
        /* wait ap3216c reset */
        mdelay(50);
        /* AP3216C ALS and PS+IR functions actions */
        ap3216c_write_single_reg(&ap3216c_dev, AP3216C_SYS_CONFIG, 0x03);
        /* Verify that the Settings are successful */
        if (ap3216c_read_single_reg(&ap3216c_dev, AP3216C_SYS_CONFIG) == 0x03)
                ret = 0;
        else ret = -1;

        return ret;
}

static ssize_t ap3216c_read(struct file *filp,
                        char __user *user,
                        size_t count,
                        loff_t *loffp)
{
        int ret = 0;
        unsigned short buf[3] = {0};
        struct ap3216c_device *dev = filp->private_data;

        ap3216c_readdata(dev);
        buf[0] = dev->ir;
        buf[1] = dev->ps;
        buf[2] = dev->als;

        ret = copy_to_user(user, buf, sizeof(buf));

        return ret;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
        filp->private_data = NULL;
        printk("ap3216c release!\n");
        return 0;
}

/* 传统匹配方式(无设备树)ID列表 */
static const struct i2c_device_id ap3216c_id_table[] = {
        {"ap3216c", 0},
        {}
};

/* 设备树匹配方式列表 */
static const struct of_device_id ap3216c_match_table[] = {
        { .compatible = "alientek,ap3216c"},
        {}
};

static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
        int ret = 0;
        if (ap3216c_dev.major) {
                ap3216c_dev.devid = MKDEV(ap3216c_dev.major, ap3216c_dev.minor);
                ret = register_chrdev_region(ap3216c_dev.devid, DEVICE_CNT, DEVICE_NAME);
        } else {
                ret = alloc_chrdev_region(&ap3216c_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_chrdev_region;
        }
        ap3216c_dev.major = MAJOR(ap3216c_dev.devid);
        ap3216c_dev.minor = MINOR(ap3216c_dev.devid);
        printk("major:%d minor:%d\n", ap3216c_dev.major, ap3216c_dev.minor);

        cdev_init(&ap3216c_dev.ap3216c_cdev, &ops);
        ret = cdev_add(&ap3216c_dev.ap3216c_cdev, ap3216c_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }
        ap3216c_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(ap3216c_dev.class)) {
                printk("class create error!\n");
                ret = -EINVAL;
                goto fail_class_create;
        }
        ap3216c_dev.device = device_create(ap3216c_dev.class, NULL,
                                       ap3216c_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(ap3216c_dev.device)) {
                printk("device create error!\n");
                ret = -EINVAL;
                goto fail_device_create;
        }
        ap3216c_dev.client = client;
        goto success;
        
//fail_io_config:
        device_destroy(ap3216c_dev.class, ap3216c_dev.devid);
fail_device_create:
        class_destroy(ap3216c_dev.class);
fail_class_create:
        cdev_del(&ap3216c_dev.ap3216c_cdev);
fail_cdev_add:
        unregister_chrdev_region(ap3216c_dev.devid, DEVICE_CNT);
fail_chrdev_region:
success:
        return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
        printk("ap3216c_remove!\n");
        device_destroy(ap3216c_dev.class, ap3216c_dev.devid);
        class_destroy(ap3216c_dev.class);
        cdev_del(&ap3216c_dev.ap3216c_cdev);
        unregister_chrdev_region(ap3216c_dev.devid, DEVICE_CNT);
        return 0;
}

static struct i2c_driver ap3216c_driver = {
        .driver = {
                .name = "ap3216c",
                .owner = THIS_MODULE,
                .of_match_table = ap3216c_match_table,  /* 设备树方式驱动匹配 */
        },
        .probe = ap3216c_probe,
        .remove = ap3216c_remove,
        .id_table = ap3216c_id_table, /* 适用于无设备树驱动时匹配 */
};

static int __init ap3216c_init(void)
{
        i2c_register_driver(THIS_MODULE, &ap3216c_driver);
        return 0;
}

static void __exit ap3216c_exit(void)
{
        i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");