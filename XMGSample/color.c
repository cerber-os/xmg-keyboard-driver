#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XMG_MAGIC_CODE      'X'
#define XMG_SET_COLOR       _IOW(XMG_MAGIC_CODE, 0x01, int)

int main(int argc, char** argv) {
	if(argc < 4) {
		fprintf(stderr, "Usage: %s <r> <g> <b>\n", argv[0]);
		exit(1);
	}

	int red = atoi(argv[1]);
    int green = atoi(argv[2]);
    int blue = atoi(argv[3]);
    int color = blue << 16 | red << 8 | green;

	int fd = open("/dev/xmg_driver", 0);
	if(fd < 0) {
		perror("open");
		exit(1);
	}

	int ret = ioctl(fd, XMG_SET_COLOR, color);
	if(ret < 0) {
		perror("ioctl");
		exit(1);
	}

	close(fd);
}
