#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct xmg_dchu {
    int             cmd;
    char*           ubuf;
    int             length;
};

#define XMG_MAGIC_CODE      'X'
#define XMG_CALL_DCHU       _IOWR(XMG_MAGIC_CODE, 0x10, struct xmg_dchu*)



int main(int argc, char** argv) {
    char buffer[4096];

	if(argc < 2) {
		fprintf(stderr, "Usage: %s <cmd>", argv[0]);
		exit(1);
	}

	int cmd = atoi(argv[1]);
    size_t len = read(0, buffer, 4096);

    struct xmg_dchu dchu = {
        .cmd = cmd,
        .length = len,
        .ubuf = buffer
    };

	int fd = open("/dev/xmg_driver", 0);
	if(fd < 0) {
		perror("open");
		exit(1);
	}

	int ret = ioctl(fd, XMG_CALL_DCHU, &dchu);
	if(ret < 0) {
		perror("ioctl");
		exit(1);
	}

	close(fd);
}
