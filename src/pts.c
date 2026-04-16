#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

// 打印hex（调试RPMSG非常关键）
void dump_hex(const char *prefix, unsigned char *buf, int len) {
    printf("%s [%d]: ", prefix, len);
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
}

// 设置raw模式
void set_raw(int fd) {
    struct termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tcsetattr(fd, TCSANOW, &tio);
}



int main(int argc, char *argv[]) {
    int master_fd;
    int slave_fd = -1;
    char *slave_name;
    int rpmsg_fd   = -1;
    int use_bridge = 0;

    if (argc == 2) {
        // bridge模式
        rpmsg_fd = open(argv[1], O_RDWR | O_NOCTTY);
        if (rpmsg_fd < 0) {
            perror("open rpmsg");
            return -1;
        }
        set_raw(rpmsg_fd);
        use_bridge = 1;
        printf("Bridge to: %s\n", argv[1]);
    } else {
        printf("Loopback mode\n");
    }

    // 创建PTY
    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    grantpt(master_fd);
    unlockpt(master_fd);
    slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
        perror("open slave pty");
        close(master_fd);
        return -1;
    }

    // 避免slave侧默认ECHO导致回环失控
    set_raw(slave_fd);

    printf("Virtual TTY: %s\n", slave_name);
    printf("Slave configured: raw -echo\n\n");

    unsigned char buf[1024];

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);

        FD_SET(master_fd, &rfds);
        int maxfd = master_fd;

        if (use_bridge) {
            FD_SET(rpmsg_fd, &rfds);
            if (rpmsg_fd > maxfd) maxfd = rpmsg_fd;
        }

        int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        // PTY -> RPMSG / loopback
        if (FD_ISSET(master_fd, &rfds)) {
            int len = read(master_fd, buf, sizeof(buf));
            if (len > 0) {
                dump_hex("PTY RX", buf, len);

                if (use_bridge) {
                    write(rpmsg_fd, buf, len);
                } else {
                    write(master_fd, buf, len);  // loopback
                }
            }
        }

        // RPMSG -> PTY
        if (use_bridge && FD_ISSET(rpmsg_fd, &rfds)) {
            int len = read(rpmsg_fd, buf, sizeof(buf));
            if (len > 0) {
                dump_hex("RPMSG RX", buf, len);
                write(master_fd, buf, len);
            }
        }
    }

    close(slave_fd);
    close(master_fd);
    if (rpmsg_fd >= 0) {
        close(rpmsg_fd);
    }

    return 0;
}