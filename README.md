# Linux toolset for controlling keyboard backlight on some XMG laptops

Linux kernel driver and userspace toolset for interacting with keyboard backlight used in a few models of XMG laptops.

**Note:** This isn't official software developed by manufacturer, but rather a custom piece of code made by a few passionates.

## Installation
If you're using Arch Linux or other distro using the same package manager (pacman), reach out to Github Releases tab and download the newest `.tar.zst` archive. After that install it with the following command:

```sh
sudo pacman -U <path_to_file>.tar.zst
```

After reboot kernel driver should be fully operational.


In other cases, manually install kernel driver and userspace toolset with help from `README.md` files in `cli/` and `driver/` directories.

## Tested hardware
List of tested laptop models:

| Model | Result | Notes |
| -- | -- | -- |
| XMG Apex 15 | OK :heavy_check_mark: | - |
