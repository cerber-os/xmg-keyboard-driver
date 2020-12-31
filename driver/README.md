# Linux kernel ACPI driver

Linux kernel driver adding support for controlling keyboard lightning via ACPI interface. Basicaly, this is just convienient wrapper for Device Specific Method (`_DSM`) defined in ACPI rules of keyboard controller.

## Building and running
Make sure Linux headers are installed in your system and present under `/lib/modules/<kernel_version>/build` directory. On most distros, this can be done by installing package named `linux-headers`. For instance to install headers on Arch Linux run:

```sh
sudo pacman -S linux-headers
```

After header files have been installed, enter this directory and run:

```sh
make
```

After a brief moment kernel module named `xmg_driver.ko` should be present in build directory. To load it into the running kernel, run:

```sh
insmod xmg_driver.ko
```

Confirm that driver was properly loaded:

```sh
dmesg | grep xmg_driver
# [ 3548.189030] xmg_driver CLV0001:00: registered (v.X.X)
# ...
```


## Interfaces
After loading driver created char device `/dev/xmg_driver`, which supports the following IOCTL commands:

| Name | Input type | Notes |
| ---- | ---------- | ----- |
| XMG_SET_BRIGHTNESS | int | Set keyboard brightness to provided value. Valid range: `0 - 191` |
| XMG_SET_COLOR | int | Set keyboard color. Value should be encoded as 24-bit number in format `BBRRGG` (yeah, not very intuitive, but that's how format used by keyboard controller looks like) |
| XMG_SET_TIMEOUT | int | Set length of inactivity after which keyboard will disable lightning. Valid range: `0 - 0xffff` |
| XMG_SET_BOOT | int | Overwrite boot effect of keyboard with current settings |
| XMG_CALL_DCHU | struct xmg_dchu* | Send raw DCHU package to keyboard controller - useful for development. Accessible only to processes with `CAP_SYS_ADMIN` capability |

