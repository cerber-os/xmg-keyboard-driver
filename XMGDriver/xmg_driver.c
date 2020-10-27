/*  
 *  XMGDriver.c - Driver adding support for ACPI specific functionality
 *  	present on XMG laptop
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include "xmg_driver.h"

#ifndef CONFIG_ACPI
	#error "CONFIG_ACPI is required by XMGDriver!"
#endif


/*
 *	ACPI support
 */
static const char DCHU_UUID[] = {0xe4, 0x24, 0xf2, 0x93, 0xdc, 0xfb, 0xbf, 0x4b, 0xad, 0xd6, 0xdb, 0x71, 0xbd, 0xc0, 0xaf, 0xad};


#define ACPI_SETUP_BUFFER(SRC, BUF, LEN)                \
                do {                                    \
                (SRC).type = ACPI_TYPE_BUFFER;          \
                (SRC).buffer.pointer = (char*)(BUF);    \
                (SRC).buffer.length = (LEN);            \
                } while(0)
#define ACPI_SETUP_INTEGER(SRC, VALUE)                  \
                do {                                    \
                (SRC).type = ACPI_TYPE_INTEGER;         \
                (SRC).integer.value = (VALUE);          \
                } while(0)
#define ACPI_SETUP_PACKAGE(SRC, PKGS, CNT)                                  \
                do {                                                        \
                (SRC).type = ACPI_TYPE_PACKAGE;                             \
                (SRC).package.elements = (union acpi_object *)(PKGS);       \
                (SRC).package.count = (CNT);                                \
                } while(0)




static int xmgAcpiCall(struct platform_device* device, char* buffer, size_t buffer_len) {
    int i, ret = 0;
	acpi_status acpiStatus;
    struct acpi_object_list arg;
    struct acpi_buffer out_buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    union acpi_object* packages;

    if(buffer_len < 0x10) {
        dev_err(&device->dev, "[%s] Buffer must have at least 0x10 bytes (got: %lu)", __func__, buffer_len);
        ret = -EINVAL;
        goto exit;
    }


    arg.count = 4;
    arg.pointer = kmalloc(arg.count * sizeof(union acpi_object), GFP_KERNEL);
    if(!arg.pointer) {
        ret = -ENOMEM;
        goto exit;
    }

    ACPI_SETUP_BUFFER(arg.pointer[0], DCHU_UUID, sizeof(DCHU_UUID));
    ACPI_SETUP_INTEGER(arg.pointer[1], 0);
    ACPI_SETUP_INTEGER(arg.pointer[2], 103);    // CMD


    packages = kmalloc(0x104 * sizeof(union acpi_object), GFP_KERNEL);
    if(!packages) {
        ret = -ENOMEM;
        goto arg_free;
    }

    ACPI_SETUP_BUFFER(packages[0], buffer, buffer_len);
    for(i = 1; i < 0x104; i++)
        ACPI_SETUP_INTEGER(packages[i], 0);
    ACPI_SETUP_PACKAGE(arg.pointer[3], packages, 0x104);


    acpiStatus = acpi_evaluate_object(ACPI_HANDLE(&device->dev), "_DSM", &arg, &out_buffer);
    if (ACPI_FAILURE(acpiStatus))
    {
        dev_err(&device->dev, "[%s] Cannot evaluate object - ACPI Error: %s", 
				    __func__, acpi_format_exception(acpiStatus));
        ret = -EFAULT;
    }

    dev_info(&device->dev, "[%s] ACPI returned data of size %lld", __func__, out_buffer.length);

    kfree(packages);
    kfree(out_buffer.pointer);
arg_free:
    kfree(arg.pointer);
exit:
	return ret;
}


static int xmg_driver_set_brightness(struct xmg_data* xmg, int* brightness) {
    int ret = 0;
    int enc_brightness;
    char brightness_buffer[0x10] =  {0};

    if(*brightness < 0 || *brightness > 171) {
        dev_err(&xmg->pdev->dev, "[%s] Brightness level is outside of allowed range (got: %d, expected 0-171)",
                    __func__, *brightness);
        return -EINVAL;
    }

    // Encode brightness for ACPI subsystem
    enc_brightness = (0xF4 << 24) | *brightness;
    memcpy(brightness_buffer, &enc_brightness, sizeof(enc_brightness));

    ret = xmgAcpiCall(xmg->pdev, brightness_buffer, sizeof(brightness_buffer));

    if(!ret)
        memcpy(brightness, brightness_buffer, sizeof(*brightness));

    return ret;
}

/*
 *	FILE OPERATIONS IMPLEMENTATION
 */
static long xmg_driver_ioctl(struct file* file, unsigned int cmd, long unsigned int __user arg) {
    int ret = 0;
    struct xmg_data* xmg_data = container_of(file->private_data, struct xmg_data, mdev);
    
    union {
        int brightness;
        int color;
    } params;

    switch(cmd) {
        case XMG_SET_BRIGHTNESS:
            if(copy_from_user(&params.brightness, (void* __user)arg, sizeof(params.brightness))) {
                dev_err(&xmg_data->pdev->dev, "[%s] Copy from user failed", __func__);
                ret = -EINVAL;
                break;
            }

            ret = xmg_driver_set_brightness(xmg_data, &params.brightness);
            if(!ret)
                break;
            
            if(copy_to_user((void* __user)arg, &params.brightness, sizeof(params.brightness))) {
                dev_err(&xmg_data->pdev->dev, "[%s] Copy to user failed", __func__);
                ret = -EINVAL;
                break;                   
            }
            break;

        case XMG_SET_COLOR:
            ret = -ENOTSUPP;
            break;

        default:
            dev_err(&xmg_data->pdev->dev, "[%s] Invalid IOCTL code (%d)", __func__,  cmd);
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

    drv->mdev.minor  = MISC_DYNAMIC_MINOR;
    drv->mdev.name   = "xmg_driver";
    drv->mdev.fops   = &xmg_driver_fops;
    drv->mdev.parent = NULL;

    ret = misc_register(&drv->mdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to create miscdev\n");
        return ret;
    }

    dev_info(&pdev->dev, "Registered (v.%s)", XMGDriverVersionStr);
    return 0;
}

static int xmg_driver_remove(struct platform_device *pdev) {
    struct xmg_data *drv = platform_get_drvdata(pdev);

    misc_deregister(&drv->mdev);
    kfree(drv);

    dev_info(&pdev->dev, "Unregistered");
    return 0;
}

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
    },
};

module_platform_driver(xmg_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pawel Wieczorek <contact@cerberos.pl>");
MODULE_DESCRIPTION("ACPI interface support for XMG laptops");
