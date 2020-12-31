/*  
 *  xmg_driver.c - Driver adding support for keyboard lightning
 *          in some XMG laptops
 */
#include <linux/acpi.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>


#include "xmg_driver.h"

#ifndef CONFIG_ACPI
	#error "CONFIG_ACPI is required by xmg_driver!"
#endif


static const char DCHU_UUID[] = {
    0xe4, 0x24, 0xf2, 0x93, 0xdc, 0xfb, 0xbf, 0x4b, 
    0xad, 0xd6, 0xdb, 0x71, 0xbd, 0xc0, 0xaf, 0xad
};

static int xmg_acpi_call(struct device* dev, int cmd, char* buffer, size_t buffer_len) {
    int i, ret = 0;
    char stack_buffer[0x10] = {0};
    
    acpi_status acpiStatus;
    struct acpi_object_list arg;
    struct acpi_buffer out_buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    union acpi_object* packages;

    // Extend buffer to at least 0x10 bytes
    if(buffer_len < 0x10) {
        memcpy(stack_buffer, buffer, buffer_len);
        buffer = stack_buffer;
        buffer_len = 0x10;
    }

    // Prepare acpi package
    arg.count = 4;
    arg.pointer = kmalloc(arg.count * sizeof(union acpi_object), GFP_KERNEL);
    if(!arg.pointer) {
        ret = -ENOMEM;
        goto exit;
    }

    packages = kmalloc(0x104 * sizeof(union acpi_object), GFP_KERNEL);
    if(!packages) {
        ret = -ENOMEM;
        goto arg_free;
    }

    ACPI_SETUP_BUFFER(arg.pointer[0], DCHU_UUID, sizeof(DCHU_UUID));
    ACPI_SETUP_INTEGER(arg.pointer[1], 0);
    ACPI_SETUP_INTEGER(arg.pointer[2], cmd);

    ACPI_SETUP_BUFFER(packages[0], buffer, buffer_len);
    for(i = 1; i < 0x104; i++)
        ACPI_SETUP_INTEGER(packages[i], 0);
    ACPI_SETUP_PACKAGE(arg.pointer[3], packages, 0x104);


    acpiStatus = acpi_evaluate_object(ACPI_HANDLE(dev), "_DSM", &arg, &out_buffer);
    if (ACPI_FAILURE(acpiStatus))
    {
        XMG_LOG_ERR(dev, "Cannot evaluate object - ACPI Error: %s", acpi_format_exception(acpiStatus));
        ret = -EFAULT;
    }

    kfree(packages);
    kfree(out_buffer.pointer);
arg_free:
    kfree(arg.pointer);
exit:
	return ret;
}


static int xmg_driver_set_brightness(struct device* dev, int brightness) {
    int ret = 0;
    int enc_brightness;

    if(brightness < 0 || brightness > MAX_BRIGHTNESS_LEVEL) {
        XMG_LOG_ERR(dev, "Invalid brightness level (got: %d, expected 0-%d)",
            brightness, MAX_BRIGHTNESS_LEVEL);
        return -EINVAL;
    }

    enc_brightness = (KEYBOARD_BRIGHTNESS_MAGIC << 24) | brightness;
    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND, (char*)&enc_brightness, sizeof(enc_brightness));
    return ret;
}

static int xmg_driver_set_color(struct device* dev, int color) {
    int ret = 0;
    int enc_color;
    
    if(color & 0xff000000) {
        XMG_LOG_ERR(dev, "Invalid color provided (got: %x, expected 0-0xffffff)", color);
        return -EINVAL;
    }

    enc_color = (KEYBOARD_COLOR_MAGIC << 24) | color;
    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND, (char*)&enc_color, sizeof(enc_color));    
    return ret;
}

static int xmg_driver_set_timeout(struct device* dev, int timeout) {
    int ret = 0;
    int enc_timeout;

    if(timeout < 0) {
        // Disable timeout
        enc_timeout = KEYBOARD_TIMEOUT_MAGIC << 24;
    } else if(timeout > 0xffff) {
        XMG_LOG_ERR(dev, "Invalid timeout provided (got: %x, expected 0-0xffff)", timeout);
        return -EINVAL;
    } else {
        // Set timeout to X sec.
        enc_timeout = (KEYBOARD_TIMEOUT_MAGIC << 24) | (timeout << 8) | 0xFF;
    }

    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND_2, (char*)&enc_timeout, sizeof(enc_timeout));    
    return ret;
}

static int xmg_driver_set_boot(struct device* dev, int mode) {
    int ret = 0;
    int enc_mode = (KEYBOARD_BOOT_MAGIC << 24) | (!!mode);

    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND_2, (char*)&enc_mode, sizeof(enc_mode));    
    return ret;
}

static int xmg_driver_call_dchu(struct device* dev, struct xmg_dchu* dchu) {
    int ret = 0;
    char* kernel_buffer = NULL;
    
    if(!dchu->length || dchu->length > 4096)
        return -EINVAL;

    kernel_buffer = kmalloc(dchu->length, GFP_KERNEL);
    if(!kernel_buffer)
        return -ENOMEM;

    if(copy_from_user(kernel_buffer, dchu->ubuf, dchu->length)) {
        XMG_LOG_ERR(dev, "copy from user failed");
        ret = -EINVAL;
        goto exit;
    }

    ret = xmg_acpi_call(dev, dchu->cmd, kernel_buffer, dchu->length);
    if(!ret) 
        goto exit;

    if(copy_to_user(dchu->ubuf, kernel_buffer, dchu->length)) {
        XMG_LOG_ERR(dev, "copy to user failed");
        ret = -EINVAL;
        goto exit;
    }
    
exit:
    kfree(kernel_buffer);
    return ret;
}

/*
 *	FILE OPERATIONS IMPLEMENTATION
 */
static long xmg_driver_ioctl(struct file* file, unsigned int cmd, unsigned long __user arg) {
    int ret = 0;
    struct xmg_data* xmg_data = container_of(file->private_data, struct xmg_data, mdev);
    struct device* dev = &xmg_data->pdev->dev;
    union {
        struct xmg_dchu dchu;
    } params;

    switch(cmd) {
        case XMG_SET_BRIGHTNESS:
            ret = xmg_driver_set_brightness(dev, (int)arg);
            if(ret)
                break;

            atomic_set(&xmg_data->brightness, (int)arg);
            break;

        case XMG_SET_COLOR:
            ret = xmg_driver_set_color(dev, (int)arg);
            if(ret)
                break;
                
            atomic_set(&xmg_data->color, (int)arg);
            break;

        case XMG_SET_TIMEOUT:
            ret = xmg_driver_set_timeout(dev, (int)arg);
            if(ret)
                break;
                
            atomic_set(&xmg_data->timeout, (int)arg);
            break;

        case XMG_SET_BOOT:
            ret = xmg_driver_set_boot(dev, (int)arg);          
            break;

        case XMG_CALL_DCHU:
            if(!capable(CAP_SYS_ADMIN)) {
                XMG_LOG_ERR(dev, "Access to XMG_CALL_DCHU requires CAP_SYS_ADMIN capability");
                ret = -EPERM;
                break;
            }

            if(copy_from_user(&params.dchu, (void* __user)arg, sizeof(params.dchu))) {
                XMG_LOG_ERR(dev, "copy from user failed");
                ret = -EINVAL;
                break;
            }

            ret = xmg_driver_call_dchu(dev, &params.dchu);
            // copy_to_user is not required as no changes have been made in params.dchu structure
            break;

        default:
            XMG_LOG_ERR(dev, "Invalid IOCTL code (%d)",  cmd);
            ret = -ENOTSUPP;
            break;
    }

    return ret;
}

static const struct file_operations xmg_driver_fops = {
    .owner  = THIS_MODULE,
    .unlocked_ioctl = xmg_driver_ioctl
};


/*
 *	PLATFORM DEVICE IMPLEMENTATION
 */
static int xmg_driver_probe(struct platform_device *pdev) {
    struct xmg_data *drv;
    int ret;

    drv = kmalloc(sizeof(struct xmg_data), GFP_KERNEL);
    if (!drv)
        return -ENOMEM;

    platform_set_drvdata(pdev, drv);
    drv->pdev = pdev;

    drv->mdev.name   = "xmg_driver";
    drv->mdev.fops   = &xmg_driver_fops;
    drv->mdev.minor  = MISC_DYNAMIC_MINOR;
    drv->mdev.parent = NULL;

    ret = misc_register(&drv->mdev);
    if (ret) {
        XMG_LOG_ERR(&pdev->dev, "failed to create miscdev\n");
        return ret;
    }

    atomic_set(&drv->brightness, 0);
    atomic_set(&drv->color, 0);
    atomic_set(&drv->timeout, 0);

    dev_info(&pdev->dev, "registered (v.%s)", XMGDriverVersionStr);
    return 0;
}

static int xmg_driver_remove(struct platform_device *pdev) {
    struct xmg_data *drv = platform_get_drvdata(pdev);

    misc_deregister(&drv->mdev);
    kfree(drv);

    dev_info(&pdev->dev, "unregistered");
    return 0;
}

// Function invoked after resume from suspend
//  Set up keyboard color as it it lost after each suspend/power-off
static int xmg_driver_resume(struct device *device) {
    int ret;
    struct xmg_data *drv = dev_get_drvdata(device);
    struct device* dev = &drv->pdev->dev;
    int color = atomic_read(&drv->color);
    int timeout = atomic_read(&drv->timeout);

    if(color) {
        ret = xmg_driver_set_color(dev, color);
        if(ret)
            XMG_LOG_ERR(dev, "failed to set color after resume");
    }

    if(timeout) {
        ret = xmg_driver_set_timeout(dev, timeout);
        if(ret)
            XMG_LOG_ERR(dev, "failed to set timeout after resume");
    }

    dev_info(dev, "finished resume procedure");
    return 0;
}

static const struct dev_pm_ops xmg_driver_pm_ops = {
	.resume		= xmg_driver_resume,
};

static const struct acpi_device_id xmg_driver_acpi_match[] = {
        { "CLV0001", 0 },
        { },
};
MODULE_DEVICE_TABLE(acpi, xmg_driver_acpi_match);

static struct platform_driver xmg_driver = {
    .probe      = xmg_driver_probe,
    .remove     = xmg_driver_remove,
    .driver = {
        .name = "xmg_driver",
		.acpi_match_table = ACPI_PTR(xmg_driver_acpi_match),
        .pm = &xmg_driver_pm_ops,
    },
};

module_platform_driver(xmg_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pawel Wieczorek <contact@cerberos.pl>");
MODULE_DESCRIPTION("ACPI interface support for keyboard lightning in some XMG laptops");
