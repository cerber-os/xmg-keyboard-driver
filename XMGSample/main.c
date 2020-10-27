#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XMG_MAGIC_CODE      'X'
#define XMG_SET_BRIGHTNESS  _IOWR(XMG_MAGIC_CODE, 0x00, int*)

int main(int argc, char** argv) {
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <brightness>", argv[0]);
		exit(1);
	}

	int brightness = atoi(argv[1]);

	int fd = open("/dev/xmg_driver", 0);
	if(fd < 0) {
		perror("open");
		exit(1);
	}

	int ret = ioctl(fd, XMG_SET_BRIGHTNESS, &brightness);
	if(ret < 0) {
		perror("ioctl");
		exit(1);
	}

	close(fd);
}
