#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
struct kitty_cat {
        char *name;
        char saymeow[5];
        struct class *cat_class;
        int age;
        struct cdev *cdev;
        dev_t dev;
        unsigned int major;
        unsigned int minor_start;
        struct device *catdev;
        struct mutex cat_mutex;
        wait_queue_head_t wait_q;
};
struct kitty_cat *kittycat;
static int kitty_cat_open(struct inode *inode, struct file *file)
{
        return 0;
}
static int kitty_cat_close(struct inode *inode, struct file *file)
{
        return 0;
}
static ssize_t kitty_cat_write(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
        return 0;
}
static ssize_t kitty_cat_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
        return 0;
}
static const struct file_operations kitty_cat_fops = {
        .owner = THIS_MODULE,
        .open = kitty_cat_open,
        .release = kitty_cat_close,
        .read = kitty_cat_read,
        .write = kitty_cat_write,
};
static void kitty_cat_saymeow(void)
{
        strlcpy(kittycat->saymeow, "MEOW", sizeof(kittycat->saymeow));
        pr_info("%s: Kitty cat said: %s\n", __func__, kittycat->saymeow);
}
static int kitty_cat_setup_cdev(dev_t dev)
{
        int ret;
        cdev_init(kittycat->cdev, &kitty_cat_fops);
        kittycat->cdev->owner = THIS_MODULE;
        kittycat->cdev->ops = &kitty_cat_fops;
        ret = cdev_add(kittycat->cdev, dev, 1);
        if (ret) {
                pr_info("%s: Failed to allocate region for catdev (ret: %d)\n",
                        __func__, ret);
                goto err;
        }
        kittycat->cat_class = class_create(THIS_MODULE, "kittycat");
        if (IS_ERR(kittycat->cat_class)) {
                pr_err("Error creating kittycat-> class.\n");
                return PTR_ERR(kittycat->cat_class);
        }
        kittycat->catdev = device_create(kittycat->cat_class,
                NULL, dev, (void *)kittycat, "kittycat");
        if (!kittycat->catdev)
                return -EIO;
        return 0;
err:
        return ret;
}

static int __init kitty_cat_init(void)
{
        int ret = 0;
        dev_t dev;
        kittycat = kzalloc(sizeof(struct kitty_cat) + 5, GFP_KERNEL);
        if (!kittycat) {
                pr_err("%s: kittycat allocation failed!\n", __func__);
                return -ENOMEM;
        }
        //mutex_init(&kittycat->cat_mutex);
        //init_waitqueue_head(&kittycat->wait_q);

	kittycat->name = ((void *)kittycat) + sizeof(struct kitty_cat);

        strlcpy(kittycat->name, "kittycat", 8);
	if (kittycat->name != NULL)
	        pr_info("cat name: %s\n", kittycat->name);
	else
		pr_err("kittycat->name is NULL\n");

        ret = alloc_chrdev_region(&dev, kittycat->minor_start,
                                    1, kittycat->name);
        if (!ret) {
                kittycat->major = MAJOR(dev);
                kittycat->minor_start = MINOR(dev);
        } else {
                pr_err("Major number not allocated\n");
                return ret;
        }
        kittycat->cdev = cdev_alloc();
        ret = kitty_cat_setup_cdev(dev);
        if (ret) {
                pr_info("%s failed! (ret: %d)\n", __func__, ret);
                return ret;
        }
        pr_info("%s: success!", __func__);
        pr_info("%s: Allocated catdev at major: %d, minor: %d\n",
                __func__, kittycat->major, kittycat->minor_start);
//      kitty_cat_saymeow(catdev);
        return ret;
}
static void __exit kitty_cat_exit(void)
{
        pr_info("%s\n", __func__);
        if (kittycat) {
                if (kittycat->cdev) {
                        device_destroy(kittycat->cat_class,
                                       MKDEV(kittycat->major,
                                             kittycat->minor_start));
                        cdev_del(kittycat->cdev);
                }
                if (!IS_ERR(kittycat->cat_class))
                        class_destroy(kittycat->cat_class);
                kfree(kittycat);
        }
}
core_initcall(kitty_cat_init);
module_exit(kitty_cat_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Utsav Balar <utsavbalar1231@gmail.com>");
MODULE_DESCRIPTION("A simple driver that says meow on init");
