/*  
 *  xmg_driver.h - Driver adding support for keyboard lightning
 *          in some XMG laptops
 */

#define XMGDriverVersionStr	"1.7"

struct xmg_data {
    struct platform_device* pdev;
    struct miscdevice mdev;
    
    // Remembered state
    atomic_t brightness;
    atomic_t color;
    atomic_t timeout;
};


/*
 *	MAGIC CODES USED BY _DSM
 */
#define KEYBOARD_DCHU_COMMAND       103
#define KEYBOARD_DCHU_COMMAND_2     121

#define KEYBOARD_COLOR_MAGIC        0xF0
#define KEYBOARD_BRIGHTNESS_MAGIC   0xF4
#define KEYBOARD_TIMEOUT_MAGIC      0x18
#define KEYBOARD_BOOT_MAGIC         0x18

#define MAX_BRIGHTNESS_LEVEL        191


/*
 *	LOGGING UTILS
 */
#define XMG_LOG_ERR(DEV, FMT, ...)   dev_err(DEV, "[%s] " FMT, __func__, ##__VA_ARGS__)

/*
 *	ACPI HELPERS
 */
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

/*
 *	IOCTL STRUCTURES
 */
struct xmg_dchu {
    int             cmd;
    char* __user    ubuf;
    int             length;
};


/*
 *	IOCTL CODES
 */
#define XMG_MAGIC_CODE      'X'
#define XMG_SET_BRIGHTNESS  _IOW(XMG_MAGIC_CODE, 0x00, int)
#define XMG_SET_COLOR       _IOW(XMG_MAGIC_CODE, 0x01, int)
#define XMG_SET_TIMEOUT     _IOW(XMG_MAGIC_CODE, 0x02, int)
#define XMG_SET_BOOT        _IOW(XMG_MAGIC_CODE, 0x03, int)
#define XMG_CALL_DCHU       _IOWR(XMG_MAGIC_CODE, 0x10, struct xmg_dchu*)
