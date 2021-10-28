#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>

int main(int argc, char *argv[])
{
        int ret = 0;
        int fd;
        unsigned short buf[3] = {0};
        char *filename = NULL;

        if (argc != 2) {
                printf("Error Usage!\n"
                       "Usage: %s filename\n", argv[0]);
                ret = -1;
                goto error;
        }

        filename = argv[1];
        fd = open(filename, O_RDWR);
        if (fd == -1) {
                perror("open failed!\n");
                ret = -1;
                goto error;
        }

        while (1)
        {
                ret = read(fd, buf, sizeof(buf));
                if (ret < 0)
                {
                        perror("read error");
                        goto error;
                }
                printf("ir:%d ps:%d als:%d\n", buf[0], buf[1], buf[2]);
                memset(buf, 0, sizeof(buf));
                usleep(200000);
        }

error:
        close(fd);
        return ret;
}
