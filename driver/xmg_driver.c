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
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>


#include "xmg_driver.h"

#ifndef CONFIG_ACPI
	#error "CONFIG_ACPI is required by xmg_driver!"
#endif


static const char DCHU_UUID[] = {
    0xe4, 0x24, 0xf2, 0x93, 0xdc, 0xfb, 0xbf, 0x4b, 
    0xad, 0xd6, 0xdb, 0x71, 0xbd, 0xc0, 0xaf, 0xad
};

static int xmg_acpi_call(struct device* dev, int cmd,
            char* buffer, size_t buffer_len, struct acpi_buffer* output) {
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
    if (ACPI_FAILURE(acpiStatus)) {
        XMG_LOG_ERR(dev, "Cannot evaluate object - ACPI Error: %s", acpi_format_exception(acpiStatus));
        ret = -EFAULT;
    }

    if(output != NULL) {
        memcpy(output, &out_buffer, sizeof(out_buffer));
    } else
        kfree(out_buffer.pointer);

    kfree(packages);
arg_free:
    kfree(arg.pointer);
exit:
	return ret;
}

/*
 * KEYBOARD BACKLIGHT SUPPORT
 */
static int xmg_driver_set_brightness(struct device* dev, int brightness) {
    int ret = 0;
    int enc_brightness;

    if(brightness < 0 || brightness > MAX_BRIGHTNESS_LEVEL) {
        XMG_LOG_ERR(dev, "Invalid brightness level (got: %d, expected 0-%d)",
            brightness, MAX_BRIGHTNESS_LEVEL);
        return -EINVAL;
    }

    enc_brightness = (KEYBOARD_BRIGHTNESS_MAGIC << 24) | brightness;
    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND, (char*)&enc_brightness, sizeof(enc_brightness), NULL);
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
    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND, (char*)&enc_color, sizeof(enc_color), NULL);
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

    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND_2, (char*)&enc_timeout, sizeof(enc_timeout), NULL);
    return ret;
}

static int xmg_driver_set_boot(struct device* dev, int mode) {
    int ret = 0;
    int enc_mode = (KEYBOARD_BOOT_MAGIC << 24) | (!!mode);

    ret = xmg_acpi_call(dev, KEYBOARD_DCHU_COMMAND_2, (char*)&enc_mode, sizeof(enc_mode), NULL);
    return ret;
}

/*
 * HWMON SUPPORT
 */
struct xmg_fan_acpi_response {
    u8      reserved1[2];
    u16     cpu_rpm;
    u16     gpu_rpm;
    u16     gpu2_rpm;
    u8      reserved2[8];

    u8      cpu_duty;
    u8      reserved3[1];
    u8      cpu_temp;
    u8      gpu_duty;
    u8      reserved4[1];
    u8      gpu_temp;
    u8      gpu2_duty;
    u8      reserved5[1];
    u8      gpu2_temp;
} __PACKED__;


static int xmg_fan_get_data(struct device* dev, struct xmg_fan_acpi_response* fan_data) {
    int ret = 0;
    char empty_input[0x10] = {0};
    union acpi_object* acpi_obj;
    struct acpi_buffer acpi_output = {0};

    ret = xmg_acpi_call(dev, FAN_DCHU_COMMAND_GET, empty_input, 0, &acpi_output);
    if(ret)
        goto exit;
    
    /*
     * acpi_output contains pointer to acpi_object
     */
    acpi_obj = acpi_output.pointer;

    if(acpi_obj->type != ACPI_TYPE_BUFFER) {
        XMG_LOG_ERR(dev, "got invalid ACPI buffer type (%d, expected: %d)",
            acpi_obj->type, ACPI_TYPE_BUFFER);
        ret = -EFAULT;
        goto exit;
    }

    if(acpi_obj->buffer.length < sizeof(struct xmg_fan_acpi_response)) {
        XMG_LOG_ERR(dev, "got invalid ACPI buffer size (%d, expected min.: %lu)",
            acpi_obj->buffer.length, sizeof(struct xmg_fan_acpi_response));
        ret = -EFAULT;
        goto exit;
    }

    memcpy(fan_data, acpi_obj->buffer.pointer, sizeof(*fan_data));

    // Fix endianess
    fan_data->cpu_rpm = be16_to_cpu(fan_data->cpu_rpm);
    fan_data->gpu_rpm = be16_to_cpu(fan_data->gpu_rpm);
    fan_data->gpu2_rpm = be16_to_cpu(fan_data->gpu2_rpm);

exit:
    kfree(acpi_output.pointer);
    return ret;
}

static ssize_t xmg_hwmon_temp_show(struct device* hwdev,
				  struct device_attribute* devattr, char* buf) {
    int index = to_sensor_dev_attr(devattr)->index;
	struct xmg_data* xmg = dev_get_drvdata(hwdev);
    struct device* dev = &xmg->pdev->dev;

    struct xmg_fan_acpi_response fan_data;
    int temperature, ret = 0;

    ret = xmg_fan_get_data(dev, &fan_data);
    if(ret != 0) {
        XMG_LOG_ERR(dev, "failed to get fan_data (ret=%d)", ret);
        return ret;
    }

    if(index == 0)
        temperature = fan_data.cpu_temp;
    else if(index == 1)
        temperature = fan_data.gpu_temp;
    else {
        XMG_LOG_ERR(dev, "invalid sensor index (%d)", index);
        return -EINVAL;
    }
	return sprintf(buf, "%d\n", temperature * 1000);
}

static ssize_t xmg_hwmon_temp_label_show(struct device* hwdev,
                    struct device_attribute* devattr, char* buf) {
    int index = to_sensor_dev_attr(devattr)->index;

    static const char* XMG_TEMP_LABELS[] = {
        "CPU Temp",
        "GPU Temp",
    };

    if(index < 0 || index >= ARRAY_SIZE(XMG_TEMP_LABELS))
        return -EINVAL;

    return sprintf(buf, "%s\n", XMG_TEMP_LABELS[index]);
}

/*
* Convert value returned by ACPI call to rotations per minute (RPM)
*  - this formula is equivalent to `2 * 60 / (128 / 23 * acpi_value)`
*/
#define XMG_ACPI_RPM_TO_REAL(X) (2156250ull / (unsigned long long) X)

static ssize_t xmg_hwmon_fan_show(struct device* hwdev,
				  struct device_attribute* devattr, char* buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct xmg_data* xmg = dev_get_drvdata(hwdev);
    struct device* dev = &xmg->pdev->dev;

    struct xmg_fan_acpi_response fan_data;
    int fan_speed, ret = 0;

    ret = xmg_fan_get_data(dev, &fan_data);
    if(ret != 0) {
        XMG_LOG_ERR(dev, "failed to get fan_data (ret=%d)", ret);
        return ret;
    }

    if(index == 0)
        fan_speed = fan_data.cpu_rpm;
    else if(index == 1)
        fan_speed = fan_data.gpu_rpm;
    else {
        XMG_LOG_ERR(dev, "invalid sensor index (%d)", index);
        return -EINVAL;
    }

    if(fan_speed == 0) {
        XMG_LOG_WARN(dev, "CPU Fan RPM is Infinity");
        return -EFAULT;
    }

    fan_speed = XMG_ACPI_RPM_TO_REAL(fan_speed);
	return sprintf(buf, "%d\n", fan_speed);
}

static ssize_t xmg_hwmon_fan_label_show(struct device* hwdev,
                    struct device_attribute* devattr, char* buf) {
    int index = to_sensor_dev_attr(devattr)->index;

    static const char* XMG_FAN_LABELS[] = {
        "CPU Fan",
        "GPU Fan",
    };

    if(index < 0 || index >= ARRAY_SIZE(XMG_FAN_LABELS))
        return -EINVAL;
    
    return sprintf(buf, "%s\n", XMG_FAN_LABELS[index]);
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, xmg_hwmon_fan, 0);
static SENSOR_DEVICE_ATTR_RO(fan1_label, xmg_hwmon_fan_label, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, xmg_hwmon_fan, 1);
static SENSOR_DEVICE_ATTR_RO(fan2_label, xmg_hwmon_fan_label, 1);
static SENSOR_DEVICE_ATTR_RO(temp1_input, xmg_hwmon_temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_label, xmg_hwmon_temp_label, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, xmg_hwmon_temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp2_label, xmg_hwmon_temp_label, 1);

static struct attribute *xmg_acpi_attrs[] = {
    &sensor_dev_attr_fan1_input.dev_attr.attr,      /* 0 - CPU Fan RPM */
    &sensor_dev_attr_fan1_label.dev_attr.attr,      /* 1 - CPU Fan Label */
    &sensor_dev_attr_fan2_input.dev_attr.attr,      /* 2 - GPU Fan RPM */
    &sensor_dev_attr_fan2_label.dev_attr.attr,      /* 3 - GPU Fan Label */
    &sensor_dev_attr_temp1_input.dev_attr.attr,     /* 4 - CPU Temperature */
    &sensor_dev_attr_temp1_label.dev_attr.attr,     /* 5 - CPU Temp Label */
    &sensor_dev_attr_temp2_input.dev_attr.attr,     /* 6 - GPU Temperature */
    &sensor_dev_attr_temp2_label.dev_attr.attr,     /* 7 - GPU Temp Label */
    NULL,                                           /* Don't forget about NULL terminator! */
};


static umode_t xmg_acpi_is_visible(struct kobject *kobj, struct attribute *attr, int index) {
    return attr->mode;
}


static const struct attribute_group xmg_acpi_group = {
	.attrs = xmg_acpi_attrs,
	.is_visible = xmg_acpi_is_visible,
};
__ATTRIBUTE_GROUPS(xmg_acpi);

static int xmg_hwmon_init(struct xmg_data* xmg) {
    int ret = 0;

    xmg->hdev = hwmon_device_register_with_groups(NULL, "xmg_acpi", xmg, xmg_acpi_groups);
    if(IS_ERR(xmg->hdev))
        ret = PTR_ERR(xmg->hdev);
    return ret;
}

static void xmg_hwmon_remove(struct xmg_data* xmg) {
    hwmon_device_unregister(xmg->hdev);
}


/*
 * MISC
 */
static int xmg_driver_call_dchu(struct device* dev, struct xmg_dchu* dchu) {
    int ret = 0;
    char* kernel_buffer = NULL;
    struct acpi_buffer acpi_output = {0};
    union acpi_object* acpi_obj = NULL;
    
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

    ret = xmg_acpi_call(dev, dchu->cmd, kernel_buffer, dchu->length, &acpi_output);
    if(ret)
        goto exit;

    /*
     * Despite type, acpi_output contains pointer to kernel object acpi_object
     *   we need to copy only its content to user
     */
    acpi_obj = acpi_output.pointer;
    if(acpi_obj->type == ACPI_TYPE_BUFFER) {
        size_t acpi_buffer_size = acpi_obj->buffer.length;

        if(acpi_buffer_size > dchu->length) {
            XMG_LOG_WARN(dev, "ACPI output (%lu) is larger than provided buffer size (%d) - "
                    "result will be truncated!", acpi_buffer_size, dchu->length);
            acpi_buffer_size = dchu->length;
            ret = -E2BIG;
        }

        if(copy_to_user(dchu->ubuf, acpi_obj->buffer.pointer, acpi_buffer_size)) {
            XMG_LOG_ERR(dev, "copy to user failed");
            ret = -EINVAL;
            goto exit;
        }

        // Length now shows the size of saved output
        dchu->length = acpi_buffer_size;
    } else {
        XMG_LOG_ERR(dev, "ACPI output contains unsupported type (%u)", acpi_obj->type);
        ret = -ENOTSUPP;
        goto exit;
    }
exit:
    kfree(acpi_output.pointer);
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

            if(copy_to_user((void* __user)arg, &params.dchu, sizeof(params.dchu))) {
                XMG_LOG_ERR(dev, "copy to user failed");
                ret = -EINVAL;
                break;
            }

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
        XMG_LOG_ERR(&pdev->dev, "failed to create miscdev");
        return ret;
    }

    atomic_set(&drv->brightness, 0);
    atomic_set(&drv->color, 0);
    atomic_set(&drv->timeout, 0);

    // Setup cooling device for CPU fan
    ret = xmg_hwmon_init(drv);
    if(ret) {
        XMG_LOG_ERR(&drv->pdev->dev, "failed to register hwmon device - err: %d", ret);
        goto misc_unreg;
    }
    

    XMG_LOG_INFO(&pdev->dev, "registered (v.%s)", XMGDriverVersionStr);
    return 0;

misc_unreg:
    misc_deregister(&drv->mdev);
    kfree(drv);
    return ret;
}

static int xmg_driver_remove(struct platform_device *pdev) {
    struct xmg_data *drv = platform_get_drvdata(pdev);

    xmg_hwmon_remove(drv);

    misc_deregister(&drv->mdev);
    kfree(drv);

    XMG_LOG_INFO(&pdev->dev, "unregistered");
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

    XMG_LOG_INFO(dev, "finished resume procedure");
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
