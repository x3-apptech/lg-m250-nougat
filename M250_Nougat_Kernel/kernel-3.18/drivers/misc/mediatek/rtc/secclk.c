/* drivers/rtc/secclk.c
 *
 * Copyright (C) 2010-2011 LG Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/rtc.h>
#include <linux/ioctl.h>
#include <linux/module.h>

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

struct diff_time_t
{
#ifdef CONFIG_COMPAT
    compat_ulong_t diff_val;
#else
    unsigned long diff_val;
#endif
};

#define SECCLK_IOCTL_BASE     's'
#define SECCLK_IOR(nr,type)   _IOR(SECCLK_IOCTL_BASE,nr,type)
#define SECCLK_IOCTL_GET_DIFF SECCLK_IOR(0x01, struct diff_time_t)
#define SECCLK_IOCTL_ERR_OVERFLOW 1000
#define SECCLK_IOCTL_ERR_COPYTOUSER 2000

DECLARE_WAIT_QUEUE_HEAD(secclk_wait_queue);
DEFINE_SPINLOCK(secclk_lock);

static unsigned long secclk_diff_time = 0;
static int secclk_wakeup_sign = 0;

/******************************************************************************
**
**  Function:     secclk_rtc_changed
**
**  Description:  Notify deivce's RTC device try to set new time value
**
**  Arguments:
**                [fp_read_rtc] -- Fuction pointer to read device's rtc
**                [dev]         -- RTC device
**                [tm]          -- time value to be set
**
**  Returns:      0 on Success / -1 on Failure
**
******************************************************************************/
int secclk_rtc_changed(int (*fp_read_rtc)(struct device *, struct rtc_time *), struct device *dev, struct rtc_time *tm)
{
    struct rtc_time tm_temp;
    unsigned long prev_time = 0, new_time = 0;

    if(!fp_read_rtc || !dev || !tm) {
        printk("secure clock: invalid prameter (%p/%p/%p)\n", fp_read_rtc ,dev, tm);
        return -1;
    }

    /* convret new time to ucs sec */
    if (0 != rtc_tm_to_time(tm, &new_time)) {
        printk("secure clock: fail to convert new time\n");
        return -1;
    }

    /* read current time */
    if (0 != fp_read_rtc(dev, &tm_temp)) {
        printk("secure clock: read rtc failed\n");
        return -1;
    }

    /* convret previous time to ucs sec */
    if (0 != rtc_tm_to_time(&tm_temp, &prev_time)) {
        printk("secure clock: fail to convert cur time\n");
        return -1;
    }

    spin_lock(&secclk_lock);
    if (new_time < prev_time) {
        secclk_diff_time = prev_time - new_time;
        secclk_wakeup_sign = 1;
    } else {
        secclk_diff_time = new_time - prev_time;
        secclk_wakeup_sign = -1;
    }
    spin_unlock(&secclk_lock);

    if (!secclk_diff_time) {
        /*
          No need to update any of them.
          If a user selected auto time sync, it'll try time sync with network.
          But the probability of setting the same time with current time is over 95%.
          So simply skip that case.
        */
        return 0;
    }

    if (0) {
        struct rtc_time tm_prev;
        rtc_time_to_tm(prev_time, &tm_prev);

        printk("clock update : diff = %lu\n", secclk_diff_time);
        printk("new : %04d%02d%02d.%02d%02d%02d\n", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
        printk("cur : %04d%02d%02d.%02d%02d%02d\n", tm_prev.tm_year+1900, tm_prev.tm_mon+1, tm_prev.tm_mday, tm_prev.tm_hour, tm_prev.tm_min, tm_prev.tm_sec);
    }

    wake_up_interruptible(&secclk_wait_queue);

    return 0;
}

static inline long difftime_copy_to_user(void __user *to, const void *from)
{
    struct diff_time_t st_diff_time = {0,};
#ifdef CONFIG_COMPAT
    unsigned int max_uint_val = ~0U;
    st_diff_time.diff_val = *(compat_ulong_t *)from;

    if(*(unsigned long *)from > (unsigned long)max_uint_val)
    {
        printk("difftime_copy_to_user : diff is larger than 32bit max!\n");
        return -SECCLK_IOCTL_ERR_OVERFLOW;
    }
    return copy_to_user(compat_ptr((compat_uptr_t)to), &st_diff_time.diff_val, sizeof(compat_ulong_t));
#else
    st_diff_time.diff_val = *(unsigned long *)from;
    return copy_to_user(to, &st_diff_time.diff_val, sizeof(unsigned long));
#endif
}

/*
  Return value
  0 : Returned positive diff
  1 : Returned negative diff
  -EINVAL : Unknown command
  -EAGAIN : Blocking was released by unknown (unhandled) event
  -EINTR  : Interrupted system call
  -SECCLK_IOCTL_ERR_OVERFLOW : Diff is overflowed between 64bit Kernel and 32bit User
*/
static long secclk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    unsigned long diff_time = 0;
    int sign = 0;

    switch (cmd) {
    case SECCLK_IOCTL_GET_DIFF:
        /* Block and wait */
        ret = wait_event_interruptible(secclk_wait_queue, secclk_wakeup_sign != 0);
        if (ret == -ERESTARTSYS) {
            /* system call is interrupted when kernel freezes processes to suspend device. */
            return -EINTR;
        }

        spin_lock(&secclk_lock);
        diff_time = secclk_diff_time;
        sign = secclk_wakeup_sign;
        secclk_wakeup_sign = 0;
        spin_unlock(&secclk_lock);

        if (sign != 0) {
            printk("secure clock: RTC has been updated!, secclk_wakeup_sign: %d\n", sign);
            ret = difftime_copy_to_user((void __user *)arg, (const void*)&diff_time);

            if (ret == 0){
                if (sign > 0) {
                    /* the changed time is less than the previous time (go to past) */
                    ret = 0;
                } else {
                    ret = 1;
                }
            } else if (ret > 0) {
                printk("secure clock: fail to copy data(%d)\n", ret);
                ret = -SECCLK_IOCTL_ERR_COPYTOUSER;
            }
        }
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}


static int secclk_open(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}


static int secclk_release(struct inode *inode, struct file *file)
{
    return 0;
}


static const struct file_operations secclk_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = secclk_ioctl,
    .compat_ioctl = secclk_ioctl,
    .open = secclk_open,
    .release = secclk_release,
};


static struct miscdevice secclk_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "secclk",
    .fops = &secclk_fops,
};


static int __init secclk_dev_init(void)
{
    int err;

    err = misc_register(&secclk_device);
    if (err) {
        printk("secure clock: fail to register misc device (ret=%d)\n", err);
        return err;
    }

    return 0;
}


static void __exit secclk_dev_exit(void)
{
    misc_deregister(&secclk_device);
}


module_init(secclk_dev_init);
module_exit(secclk_dev_exit);
