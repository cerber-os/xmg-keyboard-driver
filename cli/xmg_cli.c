#include <argp.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


#define XMG_MAGIC_CODE      'X'
#define XMG_SET_BRIGHTNESS  _IOW(XMG_MAGIC_CODE, 0x00, int)
#define XMG_SET_COLOR       _IOW(XMG_MAGIC_CODE, 0x01, int)
#define XMG_SET_TIMEOUT     _IOW(XMG_MAGIC_CODE, 0x02, int)
#define XMG_SET_BOOT        _IOW(XMG_MAGIC_CODE, 0x03, int)


const char *argp_program_version = "xmg_cli 1.0.1";
static char doc[] = "Console interface for interacting with xmg_driver";
static struct argp_option options[] = {
    { "brightness", 'b', "value", 0, "Set keyboard brightness" },
    { "color", 'c', "[rrr-ggg-bbb] | [+-next]", 0, "Set keyboard color" },
    { "timeout", 't', "time", 0, "Set keyboard timeout" },
    { "boot-effect", 'o', 0, 0, "Overwrite keyboard boot effect" },
    { "restore", 'r', 0, 0, "Restore settings from file" },
    { 0 }
};

enum option_state {
    DISABLED = 0,
    SET_ABSOLUTE,
    SET_RELATIVE
};
enum options_ids {
    OPTION_BRIGHTNESS,
    OPTION_COLOR,
    OPTION_TIMEOUT,
    OPTION_BOOT_EFFECT,
    OPTION_RESTORE,

    OPTION_MAX_ID
};
struct arguments {
    struct {
        enum option_state state;
        int value;
    } args[OPTION_MAX_ID];
};
struct settings {
    int value[OPTION_MAX_ID];
};

static error_t parse_opt(int key, char* arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    int color_parts[3];

    switch(key) {
        case 'b':
            if(!arg) return EINVAL;

            if(arg[0] == '+' || arg[0] == '-')
                arguments->args[OPTION_BRIGHTNESS].state = SET_RELATIVE;
            else
                arguments->args[OPTION_BRIGHTNESS].state = SET_ABSOLUTE;
            
            arguments->args[OPTION_BRIGHTNESS].value = atoi(arg);
            break;

        case 'c':
            if(!arg) return EINVAL;

            if(arg[0] == '+' || arg[0] == '-') {
                arguments->args[OPTION_COLOR].state = SET_RELATIVE;
                arguments->args[OPTION_COLOR].value = atoi(arg);
            } else {
                for(int i = 0; i < 3; i++) {
                    char* color_s = strsep(&arg, "-");
                    if(color_s == NULL) {
                        fprintf(stderr, "Invalid color format\n");
                        return EINVAL;
                    }

                    color_parts[i] = atoi(color_s) & 0xff;
                }

                arguments->args[OPTION_COLOR].state = SET_ABSOLUTE;
                arguments->args[OPTION_COLOR].value = color_parts[2] << 16 | color_parts[0] << 8 | color_parts[1];
            }
            break;

        case 't':
            if(!arg) return EINVAL;

            if(arg[0] == '+' || arg[0] == '-')
                arguments->args[OPTION_TIMEOUT].state = SET_RELATIVE;
            else
                arguments->args[OPTION_TIMEOUT].state = SET_ABSOLUTE;
            arguments->args[OPTION_TIMEOUT].value = atoi(arg);
            break;

        case 'o':
            arguments->args[OPTION_BOOT_EFFECT].state = SET_ABSOLUTE;
            arguments->args[OPTION_BOOT_EFFECT].value = 1;
            break;

        case 'r':
            arguments->args[OPTION_RESTORE].state = SET_ABSOLUTE;
            arguments->args[OPTION_RESTORE].value = 1;
            break;
        
        case ARGP_KEY_ARG:
            return 0;
        
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, 0, doc, 0, 0, 0 };

int read_settings_from_file(struct settings* out) {
    char path[120];
    snprintf(path, sizeof(path), "%s/.cache/.xmg_cli_settings.bin", getenv("HOME"));
    
    int fd = open(path, 0);
    if(fd < 0)
        return 1;

    int ret = read(fd, out, sizeof(struct settings));
    if(ret != sizeof(struct settings)) {
        perror("read_settings_from_file - read");
        return 1;
    }

    close(fd);
    return 0;
}

int write_settings_to_file(struct settings* in) {
    char path[120];
    snprintf(path, sizeof(path), "%s/.cache/.xmg_cli_settings.bin", getenv("HOME"));
    
    int fd = open(path, O_RDWR | O_TRUNC | O_CREAT, 0600);
    if(fd < 0) {
        perror("write_settings_to_file - open");
        return 1;
    }

    int ret = write(fd, in, sizeof(struct settings));
    if(ret != sizeof(struct settings)) {
        perror("write_settings_to_file - write");
        return 1;
    }

    close(fd);
    return 0;    
}


struct color {
    char* name;
    int value;
};
struct color colors[] = {
    {.name = "green",   .value = 0x0000ff},
    {.name = "red",     .value = 0x00ff00},
    {.name = "blue",    .value = 0xff0000},
    {.name = "orange",  .value = 0x00fa5a},   // 250-90-0
};
#define COLORS_COUNT  (sizeof(colors) / sizeof(colors[0]))

int color_to_id(int color) {
    for(int i = 0; i < COLORS_COUNT; i++) {
        if(colors[i].value == color)
            return i;
    }
    return 0;
}

char* color_to_string(int color) {
    for(int i = 0; i < COLORS_COUNT; i++) {
        if(colors[i].value == color)
            return colors[i].name;
    }

    char* string = malloc(30);
    snprintf(string, 30, "%d-%d-%d", (color & 0xff00) >> 8, color & 0xff, (color & 0xff0000) >> 16);
    return string;
}

char* brightness_to_string(int brit) {
    char* string = malloc(30);
    snprintf(string, 30, "%d", brit * 100 / 191);
    return string;
}


int main(int argc, char** argv) {
    struct arguments arguments;
    memset(&arguments, 0, sizeof(arguments));

    error_t error = argp_parse(&argp, argc, argv, 0, 0, &arguments);
    if(error)
        return 1;
    
    int xmg_fd = open("/dev/xmg_driver", 0);
    if(xmg_fd < 0) {
        perror("open /dev/xmg_driver failed");
        return 1;
    }

    struct settings settings;
    memset(&settings, 0, sizeof(settings));
    read_settings_from_file(&settings);

    // If started with option --restore, read settings from file and apply them
    if(arguments.args[OPTION_RESTORE].state != DISABLED) {
        int ret = ioctl(xmg_fd, XMG_SET_BRIGHTNESS, settings.value[OPTION_BRIGHTNESS]);
        ret |= ioctl(xmg_fd, XMG_SET_COLOR, settings.value[OPTION_COLOR]);
        ret |= ioctl(xmg_fd, XMG_SET_TIMEOUT, settings.value[OPTION_TIMEOUT]);

        if(settings.value[OPTION_BOOT_EFFECT])
            ret |= ioctl(xmg_fd, XMG_SET_BOOT, settings.value[OPTION_BOOT_EFFECT]);

        if(ret) {
            perror("ioctl settings restore");
            return 1;
        }
    } else {
        // Set brightness
        if(arguments.args[OPTION_BRIGHTNESS].state != DISABLED) {
            int brightness;

            if(arguments.args[OPTION_BRIGHTNESS].state == SET_ABSOLUTE)
                brightness = arguments.args[OPTION_BRIGHTNESS].value;
            else
                brightness = settings.value[OPTION_BRIGHTNESS] + arguments.args[OPTION_BRIGHTNESS].value;
            
            if(brightness < 0) brightness = 0;
            else if(brightness > 191) brightness = 191;

            int ret = ioctl(xmg_fd, XMG_SET_BRIGHTNESS, brightness);
            if(ret) {
                perror("ioctl set brightness");
                return 1;
            }
            settings.value[OPTION_BRIGHTNESS] = brightness;
        }

        // Set color
        if(arguments.args[OPTION_COLOR].state != DISABLED) {
            int color;

            if(arguments.args[OPTION_COLOR].state == SET_ABSOLUTE)
                color = arguments.args[OPTION_COLOR].value;
            else {
                int id = color_to_id(settings.value[OPTION_COLOR]);
                id += arguments.args[OPTION_COLOR].value;
                id %= (int)COLORS_COUNT;
                if(id < 0) id = COLORS_COUNT + id;
                color = colors[id].value;
            }

            int ret = ioctl(xmg_fd, XMG_SET_COLOR, color);
            if(ret) {
                perror("ioctl set color");
                return 1;
            }
            settings.value[OPTION_COLOR] = color;
        }

        // Set timeout
        if(arguments.args[OPTION_TIMEOUT].state != DISABLED) {
            int timeout;

            if(arguments.args[OPTION_TIMEOUT].state == SET_ABSOLUTE)
                timeout = arguments.args[OPTION_TIMEOUT].value;
            else
                timeout = settings.value[OPTION_TIMEOUT] + arguments.args[OPTION_TIMEOUT].value;
            
            if(timeout < 0) timeout = 0;
            else if(timeout > 0xffff) timeout = 0xffff;

            int ret = ioctl(xmg_fd, XMG_SET_TIMEOUT, timeout);
            if(ret) {
                perror("ioctl set timeout");
                return 1;
            }

            settings.value[OPTION_TIMEOUT] = timeout;
        }

        // Set boot effect
        if(arguments.args[OPTION_BOOT_EFFECT].state != DISABLED) {
            int ret = ioctl(xmg_fd, XMG_SET_BOOT, 1);
            if(ret) {
                perror("ioctl set boot");
                return 1;
            }

            settings.value[OPTION_BOOT_EFFECT] = 1;
        }
    }

    printf("[%s] %s%%\n", color_to_string(settings.value[OPTION_COLOR]), 
                    brightness_to_string(settings.value[OPTION_BRIGHTNESS]));
    write_settings_to_file(&settings);
}
