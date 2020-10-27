#define XMGDriverVersionStr	"1.0-debug"

struct xmg_data {
    struct platform_device* pdev;
    struct miscdevice mdev;
};


/*
 *	IOCTL STRUCTURES
 */


/*
 *	IOCTL CODES
 */
#define XMG_MAGIC_CODE      'X'
#define XMG_SET_BRIGHTNESS  _IOWR(XMG_MAGIC_CODE, 0x00, int*)
#define XMG_SET_COLOR       _IOWR(XMG_MAGIC_CODE, 0x01, int*)
#define XMG_SET_TIMEOUT     _IOWR(XMG_MAGIC_CODE, 0x02, int*)
#define XMG_SET_BOOT_EFFECT _IOWR(XMG_MAGIC_CODE, 0x03, int*)
