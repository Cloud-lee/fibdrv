#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>    /* kmalloc(), kfree() */
#include <linux/uaccess.h> /* copy_from_user(), copy_to_user() */

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 500

// big number data max size
#define MAX_SIZE 256

// big number
typedef struct {
    char data[MAX_SIZE];
    int size, sign;
} bn;

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static void bn_init(bn *n, int num);
static int bn_clz(char *data);
static int bn_abs_compare(char *a, char *b);
static void bn_add(bn *a, bn *b, bn *c);
static void bn_sub(bn *a, bn *b, bn *c);
static void bn_mul(bn *a, bn *b, bn *c);
// static void bn_cpy(bn *a, bn *b);
static void do_add(char *a, char *b, char *c);
static void do_sub(char *a, char *b, char *c);
static void do_mul(char *a, char *b, char *c);

#if 0
static long long fib_sequence_original(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    long long f[] = {0, 1};

    for (int i = 2; i <= k; i++) {
        f[(i & 1)] += f[((i - 1) & 1)];
    }

    return f[(k & 1)];

    /* `ISO C90 forbids variable length array ‘f’ [-Wvla]` */
#if 0    
    /* FIXME: C99 variable-length array (VLA) is not allowed in Linux kernel. */
    long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
#endif
}
#endif

#if 0
static bn fib_sequence_with_bn(unsigned long long k)
{
    bn f[3];
    bn_init(&f[0], 0);
    bn_init(&f[1], 1);
    bn_init(&f[2], 1);

    if (k < 2)
        return f[k];

    for (int i = 2; i <= k; i++) {
        bn_add(&f[0], &f[1], &f[2]);
        bn_cpy(&f[0], &f[1]);
        bn_cpy(&f[1], &f[2]);
    }

    return f[2];
}
#endif

static bn fib_sequence_fast_doubling(unsigned long long k)
{
    bn f[2];
    bn_init(&f[0], 0);  // f(k)
    bn_init(&f[1], 1);  // f(k + 1)

    if (k < 2)
        return f[k];

    // t(0) = f(k) * f(k)
    // t(1) = f(k + 1) * f(k + 1)
    // t(2) = 2 * f(k) * f(k + 1)
    bn t[3];

    // fast doubling
    // f(2k) = f(k) * [2 * f(k + 1) - f(k)]
    //       = 2 * f(k) * f(k + 1) - f(k) * f(k)
    //       = t(2) - t(0)
    // f(2k + 1) = f(k) ^ 2 + f(k + 1) ^ 2
    //           = f(k) * f(k) + f(k + 1) * f(k + 1)
    //           = t(0) + t(1)
    for (unsigned long long i = 1U << (63 - __builtin_clzll(k)); i; i >>= 1) {
        bn_mul(&f[0], &f[0], &t[0]);  // t(0)
        bn_mul(&f[1], &f[1], &t[1]);  // t(1)
        bn_mul(&f[0], &f[1], &t[2]);
        bn_add(&t[2], &t[2], &t[2]);  // t(2)
        if (i & k) {
            // next k = 2k + 1
            // f(k) = f(2k + 1) = t(0) + t(1)
            bn_add(&t[0], &t[1], &f[0]);
            // f(k + 1) = f(2k + 2) = f(2k) + f(2k + 1)
            //          = t(2) - t(0) + t(0) + t(1)
            //          = t(2) + t(1)
            bn_add(&t[2], &t[1], &f[1]);
        } else {
            // next k = 2k
            // f(k) = f(2k)
            bn_sub(&t[2], &t[0], &f[0]);
            // f(k + 1) = f(2k + 1)
            bn_add(&t[0], &t[1], &f[1]);
        }
    }
    // return f(k)
    return f[0];
}

// init big number
static void bn_init(bn *n, int num)
{
    snprintf(n->data, MAX_SIZE, "%d", abs(num));
    n->size = strlen(n->data);
    n->sign = num < 0 ? 1 : 0;
}

#if 0
static void bn_cpy(bn *a, bn *b)
{
    char buf[MAX_SIZE];
    snprintf(buf, MAX_SIZE, "%s", b->data);
    snprintf(a->data, MAX_SIZE, "%s", buf);
    a->size = b->size;
    a->sign = b->sign;
}
#endif

// count leading zero
static int bn_clz(char *data)
{
    int len = strlen(data), res = 0;
    for (int i = 0; i < len && data[i] == '0'; i++)
        res++;
    return res;
}

// compare two big number abs value
static int bn_abs_compare(char *a, char *b)
{
    int lena = strlen(a), lenb = strlen(b);
    if (lena < lenb)
        return -1;
    else if (lena > lenb)
        return 1;

    for (int i = 0; i < lena; i++) {
        if (a[i] < b[i])
            return -1;
        else if (a[i] > b[i])
            return 1;
    }

    return 0;
}

static void bn_add(bn *a, bn *b, bn *c)
{
    char res[MAX_SIZE];
    if (a->sign == b->sign) {
        c->sign = a->sign;
        if (a->size < b->size)
            do_add(b->data, a->data, res);
        else
            do_add(a->data, b->data, res);
        c->size = strlen(res);
    } else {
        int tmp = bn_abs_compare(a->data, b->data);
        if (tmp == 0) {
            c->sign = 0;
            snprintf(res, MAX_SIZE, "0");
            c->size = strlen(res);
        } else {
            if (tmp == 1) {
                c->sign = a->sign;
                do_sub(a->data, b->data, res);
            } else {
                c->sign = b->sign;
                do_sub(b->data, a->data, res);
            }
            c->size = strlen(res);
        }
    }
    char buf[MAX_SIZE];
    snprintf(buf, MAX_SIZE, "%s", res);
    snprintf(c->data, MAX_SIZE, "%s", buf);
}

static void bn_sub(bn *a, bn *b, bn *c)
{
    char res[MAX_SIZE];
    if (a->sign == b->sign) {
        int tmp = bn_abs_compare(a->data, b->data);
        if (tmp == 0) {
            c->sign = 0;
            snprintf(res, MAX_SIZE, "0");
            c->size = strlen(res);
        } else {
            if (tmp == 1) {
                c->sign = a->sign;
                do_sub(a->data, b->data, res);
            } else {
                c->sign = b->sign;
                do_sub(b->data, a->data, res);
            }
            c->size = strlen(res);
        }
    } else {
        c->sign = a->sign;
        if (a->size < b->size)
            do_add(b->data, a->data, res);
        else
            do_add(a->data, b->data, res);
        c->size = strlen(res);
    }
    char buf[MAX_SIZE];
    snprintf(buf, MAX_SIZE, "%s", res);
    snprintf(c->data, MAX_SIZE, "%s", buf);
}

static void bn_mul(bn *a, bn *b, bn *c)
{
    char res[MAX_SIZE];
    if ((a->size == 1 && a->data[0] == '0') ||
        (b->size == 1 && b->data[0] == '0')) {
        c->sign = 0;
        snprintf(res, MAX_SIZE, "0");
        c->size = strlen(res);
    } else {
        c->sign = a->sign * b->sign;
        do_mul(a->data, b->data, res);
        c->size = strlen(res);
    }

    char buf[MAX_SIZE];
    snprintf(buf, MAX_SIZE, "%s", res);
    snprintf(c->data, MAX_SIZE, "%s", buf);
}

static void do_add(char *a, char *b, char *c)
{
    int i, j, k = strlen(a) + 1;
    c[k--] = '\0';
    int sum, carry = 0;
    for (i = strlen(a) - 1, j = strlen(b) - 1; i >= 0 && j >= 0; i--, j--) {
        sum = (a[i] - '0') + (b[j] - '0') + carry;
        c[k--] = sum % 10 + '0';
        carry = sum / 10;
    }
    for (; i >= 0; i--) {
        sum = (a[i] - '0') + carry;
        c[k--] = sum % 10 + '0';
        carry = sum / 10;
    }
    if (carry)
        c[k] = carry + '0';
    else
        snprintf(c, MAX_SIZE, "%s", c + 1);
}

static void do_sub(char *a, char *b, char *c)
{
    int i, j, k = strlen(a);
    c[k--] = '\0';
    int sum, carry = 0;
    for (i = strlen(a) - 1, j = strlen(b) - 1; i >= 0 && j >= 0; i--, j--) {
        sum = (a[i] - '0') - (b[j] - '0') + carry;
        c[k--] = (sum < 0 ? (sum + 10) : sum) % 10 + '0';
        carry = sum < 0 ? -1 : 0;
    }
    for (; i >= 0; i--) {
        sum = (a[i] - '0') + carry;
        c[k--] = (sum < 0 ? (sum + 10) : sum) % 10 + '0';
        carry = sum < 0 ? -1 : 0;
    }
    int tmp = bn_clz(c);
    if (tmp)
        snprintf(c, MAX_SIZE, "%s", c + tmp);
}

static void do_mul(char *a, char *b, char *c)
{
    int l = strlen(a) + strlen(b);
    for (int m = 0; m < l; m++)
        c[m] = '0';
    c[l--] = '\0';

    for (int j = strlen(b) - 1; j >= 0; j--) {
        int carry = 0, k = l;
        for (int i = strlen(a) - 1; i >= 0; i--) {
            int sum = (a[i] - '0') * (b[j] - '0') + (c[k] - '0') + carry;
            c[k] = sum % 10 + '0';
            carry = sum / 10;
            k--;
        }
        if (carry)
            c[k] = carry + '0';
        l--;
    }

    int tmp = bn_clz(c);
    if (tmp)
        snprintf(c, MAX_SIZE, "%s", c + tmp);
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    // return (ssize_t) fib_sequence(*offset);
    bn res = fib_sequence_fast_doubling(*offset);
    char kbuf[MAX_SIZE];
    snprintf(kbuf, MAX_SIZE, "%s", res.data);
    return copy_to_user(buf, kbuf, MAX_SIZE);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
