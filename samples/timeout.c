#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XMG_MAGIC_CODE      'X'
#define XMG_SET_TIMEOUT     _IOW(XMG_MAGIC_CODE, 0x02, int)

int main(int argc, char** argv) {
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <timeout>", argv[0]);
		exit(1);
	}

	int timeout = atoi(argv[1]);

	int fd = open("/dev/xmg_driver", 0);
	if(fd < 0) {
		perror("open");
		exit(1);
	}

	int ret = ioctl(fd, XMG_SET_TIMEOUT, timeout);
	if(ret < 0) {
		perror("ioctl");
		exit(1);
	}

	close(fd);
}
