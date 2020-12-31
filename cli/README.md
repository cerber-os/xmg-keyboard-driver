# Console app for interacting with xmg_driver

Console app wrapping kernel driver functionality to let user easily control keyboard backlight functionality.

## Building
Enter `cli/` directory and run make:

```sh
make
```

## Usage
To get list of avaiable commands run:

```sh
./xmg_cli --help
Usage: xmg_cli [OPTION...]
Console interface for interacting with xmg_driver

  -b, --brightness=value     Set keyboard brightness
  -c, --color=[rrr-ggg-bbb] | [+-next]
                             Set keyboard color
  -o, --boot-effect          Overwrite keyboard boot effect
  -r, --restore              Restore settings from file
  -t, --timeout=time         Set keyboard timeout
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.
```

Example command settings keyboard brightness to 50 units and color to red:

```sh
./xmg_cli -b 50 -c 255-0-0
```

Both, `brightness` and `color` options support relative values. In case of `color`, relative values iterate over the list of a few predefined colors. Example:

```sh
# Increase brightness by 10 untis and select the next color
./xmg_cli -b +10 -c +1

# Decrease brightness by 15 units and select the previous color
./xmg_cli -b -15 -c -1
```
