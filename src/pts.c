#define _GNU_SOURCE
#include "pts.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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

struct pts_session {
    int master_fd;
    int slave_fd;
    int bridge_fd;
    int running;
    int thread_started;
    char *device_path;
    uint8_t *rx_buffer;
    size_t rx_length;
    size_t rx_capacity;
    pthread_t thread_handle;
    pthread_mutex_t lock;
};

static int ensure_rx_capacity(pts_session_t *session, size_t needed)
{
    if (needed <= session->rx_capacity) {
        return 0;
    }

    size_t new_capacity = session->rx_capacity == 0 ? 1024 : session->rx_capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    uint8_t *new_buffer = (uint8_t *)realloc(session->rx_buffer, new_capacity);
    if (new_buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    session->rx_buffer = new_buffer;
    session->rx_capacity = new_capacity;
    return 0;
}

static int append_rx_data(pts_session_t *session, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return 0;
    }

    pthread_mutex_lock(&session->lock);

    size_t needed = session->rx_length + len;
    if (ensure_rx_capacity(session, needed) != 0) {
        pthread_mutex_unlock(&session->lock);
        return -1;
    }

    memcpy(session->rx_buffer + session->rx_length, data, len);
    session->rx_length += len;

    pthread_mutex_unlock(&session->lock);
    return 0;
}

static void *pts_rx_thread(void *arg)
{
    pts_session_t *session = (pts_session_t *)arg;
    unsigned char buf[1024];

    while (1) {
        pthread_mutex_lock(&session->lock);
        int running = session->running;
        int master_fd = session->master_fd;
        int bridge_fd = session->bridge_fd;
        pthread_mutex_unlock(&session->lock);

        if (!running) {
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(master_fd, &rfds);

        int maxfd = master_fd;
        if (bridge_fd >= 0) {
            FD_SET(bridge_fd, &rfds);
            if (bridge_fd > maxfd) {
                maxfd = bridge_fd;
            }
        }

        int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (FD_ISSET(master_fd, &rfds)) {
            ssize_t len = read(master_fd, buf, sizeof(buf));
            if (len <= 0) {
                break;
            }

            if (append_rx_data(session, buf, (size_t)len) != 0) {
                break;
            }

            if (bridge_fd >= 0) {
                (void)write(bridge_fd, buf, (size_t)len);
            }
        }

        if (bridge_fd >= 0 && FD_ISSET(bridge_fd, &rfds)) {
            ssize_t len = read(bridge_fd, buf, sizeof(buf));
            if (len <= 0) {
                break;
            }

            if (append_rx_data(session, buf, (size_t)len) != 0) {
                break;
            }

            (void)write(master_fd, buf, (size_t)len);
        }
    }

    pthread_mutex_lock(&session->lock);
    session->running = 0;
    pthread_mutex_unlock(&session->lock);

    return NULL;
}

static void cleanup_session_on_error(pts_session_t *session)
{
    if (session == NULL) {
        return;
    }

    if (session->bridge_fd >= 0) {
        close(session->bridge_fd);
        session->bridge_fd = -1;
    }
    if (session->slave_fd >= 0) {
        close(session->slave_fd);
        session->slave_fd = -1;
    }
    if (session->master_fd >= 0) {
        close(session->master_fd);
        session->master_fd = -1;
    }

    free(session->rx_buffer);
    free(session->device_path);
    pthread_mutex_destroy(&session->lock);
    free(session);
}

int pts_init(pts_session_t **session_out, const char **device_path, const char *bridge_path)
{
    if (session_out == NULL || device_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    *session_out = NULL;
    *device_path = NULL;

    pts_session_t *session = (pts_session_t *)calloc(1, sizeof(*session));
    if (session == NULL) {
        errno = ENOMEM;
        return -1;
    }

    session->master_fd = -1;
    session->slave_fd = -1;
    session->bridge_fd = -1;

    if (pthread_mutex_init(&session->lock, NULL) != 0) {
        free(session);
        return -1;
    }

    session->master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (session->master_fd < 0) {
        cleanup_session_on_error(session);
        return -1;
    }

    if (grantpt(session->master_fd) != 0 || unlockpt(session->master_fd) != 0) {
        cleanup_session_on_error(session);
        return -1;
    }

    char slave_name[128];
    if (ptsname_r(session->master_fd, slave_name, sizeof(slave_name)) != 0) {
        cleanup_session_on_error(session);
        return -1;
    }

    session->device_path = strdup(slave_name);
    if (session->device_path == NULL) {
        cleanup_session_on_error(session);
        return -1;
    }

    session->slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
    if (session->slave_fd < 0) {
        cleanup_session_on_error(session);
        return -1;
    }
    set_raw(session->slave_fd);

    if (bridge_path != NULL) {
        session->bridge_fd = open(bridge_path, O_RDWR | O_NOCTTY);
        if (session->bridge_fd < 0) {
            cleanup_session_on_error(session);
            return -1;
        }
        set_raw(session->bridge_fd);
    }

    session->running = 1;
    if (pthread_create(&session->thread_handle, NULL, pts_rx_thread, session) != 0) {
        cleanup_session_on_error(session);
        return -1;
    }
    session->thread_started = 1;

    *session_out = session;
    *device_path = session->device_path;
    return 0;
}

int pts_take_rx_data(pts_session_t *session, uint8_t **data, size_t *len)
{
    if (session == NULL || data == NULL || len == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&session->lock);
    if (session->rx_length == 0) {
        *data = NULL;
        *len = 0;
        pthread_mutex_unlock(&session->lock);
        return 0;
    }

    uint8_t *buffer = (uint8_t *)malloc(session->rx_length);
    if (buffer == NULL) {
        pthread_mutex_unlock(&session->lock);
        errno = ENOMEM;
        return -1;
    }

    memcpy(buffer, session->rx_buffer, session->rx_length);
    *len = session->rx_length;
    session->rx_length = 0;
    pthread_mutex_unlock(&session->lock);

    *data = buffer;
    return 0;
}

void pts_release(pts_session_t *session)
{
    if (session == NULL) {
        return;
    }

    pthread_mutex_lock(&session->lock);
    session->running = 0;
    pthread_mutex_unlock(&session->lock);

    if (session->master_fd >= 0) {
        close(session->master_fd);
        session->master_fd = -1;
    }
    if (session->bridge_fd >= 0) {
        close(session->bridge_fd);
        session->bridge_fd = -1;
    }
    if (session->slave_fd >= 0) {
        close(session->slave_fd);
        session->slave_fd = -1;
    }

    if (session->thread_started) {
        pthread_join(session->thread_handle, NULL);
    }

    free(session->rx_buffer);
    free(session->device_path);
    pthread_mutex_destroy(&session->lock);
    free(session);
}

static int open_bridge_device(const char *path) {
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open rpmsg");
        return -1;
    }

    set_raw(fd);
    printf("Bridge to: %s\n", path);
    return fd;
}

static int create_virtual_tty(int *master_fd, int *slave_fd, const char **slave_name) {
    *master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (*master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    grantpt(*master_fd);
    unlockpt(*master_fd);

    *slave_name = ptsname(*master_fd);
    if (!*slave_name) {
        perror("ptsname");
        close(*master_fd);
        *master_fd = -1;
        return -1;
    }

    *slave_fd = open(*slave_name, O_RDWR | O_NOCTTY);
    if (*slave_fd < 0) {
        perror("open slave pty");
        close(*master_fd);
        *master_fd = -1;
        return -1;
    }

    set_raw(*slave_fd);
    return 0;
}

static void print_tty_status(const char *slave_name) {
    printf("Virtual TTY: %s\n", slave_name);
    printf("Slave configured: raw -echo\n\n");
}

static void handle_master_input(int master_fd, int rpmsg_fd, int use_bridge, unsigned char *buf, size_t buf_size) {
    int len = read(master_fd, buf, buf_size);
    if (len <= 0) {
        return;
    }

    dump_hex("PTY RX", buf, len);

    if (use_bridge) {
        write(rpmsg_fd, buf, len);
    } else {
        write(master_fd, buf, len);
    }
}

static void handle_rpmsg_input(int rpmsg_fd, int master_fd, unsigned char *buf, size_t buf_size) {
    int len = read(rpmsg_fd, buf, buf_size);
    if (len <= 0) {
        return;
    }

    dump_hex("RPMSG RX", buf, len);
    write(master_fd, buf, len);
}

static void run_event_loop(int master_fd, int rpmsg_fd, int use_bridge) {
    unsigned char buf[1024];

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);

        FD_SET(master_fd, &rfds);
        int maxfd = master_fd;

        if (use_bridge) {
            FD_SET(rpmsg_fd, &rfds);
            if (rpmsg_fd > maxfd) {
                maxfd = rpmsg_fd;
            }
        }

        int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(master_fd, &rfds)) {
            handle_master_input(master_fd, rpmsg_fd, use_bridge, buf, sizeof(buf));
        }

        if (use_bridge && FD_ISSET(rpmsg_fd, &rfds)) {
            handle_rpmsg_input(rpmsg_fd, master_fd, buf, sizeof(buf));
        }
    }
}



#ifndef PTS_NO_MAIN
int main(int argc, char *argv[]) {
    int master_fd;
    int slave_fd = -1;
    char *slave_name;
    int rpmsg_fd   = -1;
    int use_bridge = 0;

    if (argc == 2) {
        rpmsg_fd = open_bridge_device(argv[1]);
        if (rpmsg_fd < 0) {
            return -1;
        }
        use_bridge = 1;
    } else {
        printf("Loopback mode\n");
    }

    if (create_virtual_tty(&master_fd, &slave_fd, (const char **)&slave_name) < 0) {
        return -1;
    }

    print_tty_status(slave_name);
    run_event_loop(master_fd, rpmsg_fd, use_bridge);

    close(slave_fd);
    close(master_fd);
    if (rpmsg_fd >= 0) {
        close(rpmsg_fd);
    }

    return 0;
}
#endif