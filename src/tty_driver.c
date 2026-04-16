#define _GNU_SOURCE

#include "tty_driver.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int set_raw_mode(int fd)
{
	struct termios tio;
	if (tcgetattr(fd, &tio) != 0) {
		return -1;
	}
	cfmakeraw(&tio);
	if (tcsetattr(fd, TCSANOW, &tio) != 0) {
		return -1;
	}
	return 0;
}

static void flush_callback_locked(tty_driver_t *drv, tty_rx_reason_t reason)
{
	tty_rx_callback_t cb = drv->callback;
	void *cb_user = drv->callback_user_ctx;
	const size_t len = drv->rx_length;

	if (len == 0 || cb == NULL) {
		drv->rx_length = 0;
		return;
	}

	cb(drv->rx_buffer, len, reason, cb_user);
	drv->rx_length = 0;
}

static void *tty_rx_thread(void *arg)
{
	tty_driver_t *drv = (tty_driver_t *)arg;
	uint64_t last_rx_ms = now_ms();

	while (true) {
		int timeout_ms = (int)drv->idle_timeout_ms;

		pthread_mutex_lock(&drv->state_lock);
		if (!drv->running) {
			pthread_mutex_unlock(&drv->state_lock);
			break;
		}
		if (drv->rx_length > 0) {
			uint64_t elapsed = now_ms() - last_rx_ms;
			if (elapsed >= drv->idle_timeout_ms) {
				flush_callback_locked(drv, TTY_RX_REASON_IDLE);
				pthread_mutex_unlock(&drv->state_lock);
				continue;
			}
			timeout_ms = (int)(drv->idle_timeout_ms - elapsed);
		}
		pthread_mutex_unlock(&drv->state_lock);

		struct pollfd pfd;
		pfd.fd = drv->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		int pr = poll(&pfd, 1, timeout_ms);
		if (pr < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}

		if (pr == 0) {
			pthread_mutex_lock(&drv->state_lock);
			if (drv->rx_length > 0) {
				flush_callback_locked(drv, TTY_RX_REASON_IDLE);
			}
			pthread_mutex_unlock(&drv->state_lock);
			continue;
		}

		if ((pfd.revents & POLLIN) == 0) {
			if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
				break;
			}
			continue;
		}

		while (true) {
			uint8_t tmp[512];
			ssize_t n = read(drv->fd, tmp, sizeof(tmp));
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					break;
				}
				goto out;
			}
			if (n == 0) {
				goto out;
			}

			last_rx_ms = now_ms();

			size_t off = 0;
			while (off < (size_t)n) {
				pthread_mutex_lock(&drv->state_lock);

				size_t free_space = drv->rx_capacity - drv->rx_length;
				size_t copy_len = (size_t)n - off;
				if (copy_len > free_space) {
					copy_len = free_space;
				}

				if (copy_len > 0) {
					memcpy(drv->rx_buffer + drv->rx_length, tmp + off, copy_len);
					drv->rx_length += copy_len;
					off += copy_len;
				}

				if (drv->rx_length == drv->rx_capacity) {
					flush_callback_locked(drv, TTY_RX_REASON_BUFFER_FULL);
				}

				pthread_mutex_unlock(&drv->state_lock);
			}

			if ((size_t)n < sizeof(tmp)) {
				break;
			}
		}
	}

out:
	pthread_mutex_lock(&drv->state_lock);
	if (drv->rx_length > 0) {
		flush_callback_locked(drv, TTY_RX_REASON_IDLE);
	}
	drv->running = 0;
	pthread_mutex_unlock(&drv->state_lock);
	return NULL;
}

int tty_driver_open(tty_driver_t *drv, const tty_driver_config_t *cfg)
{
	if (drv == NULL || cfg == NULL || cfg->device_path == NULL || cfg->rx_buffer_size == 0 ||
		cfg->idle_timeout_ms == 0) {
		errno = EINVAL;
		return -1;
	}

	memset(drv, 0, sizeof(*drv));
	drv->fd = -1;
	drv->idle_timeout_ms = cfg->idle_timeout_ms;
	drv->rx_capacity = cfg->rx_buffer_size;

	if (pthread_mutex_init(&drv->state_lock, NULL) != 0) {
		return -1;
	}
	if (pthread_mutex_init(&drv->tx_lock, NULL) != 0) {
		pthread_mutex_destroy(&drv->state_lock);
		return -1;
	}

	drv->rx_buffer = (uint8_t *)malloc(drv->rx_capacity);
	if (drv->rx_buffer == NULL) {
		pthread_mutex_destroy(&drv->tx_lock);
		pthread_mutex_destroy(&drv->state_lock);
		errno = ENOMEM;
		return -1;
	}

	drv->fd = open(cfg->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (drv->fd < 0) {
		free(drv->rx_buffer);
		drv->rx_buffer = NULL;
		pthread_mutex_destroy(&drv->tx_lock);
		pthread_mutex_destroy(&drv->state_lock);
		return -1;
	}

	if (set_raw_mode(drv->fd) != 0) {
		close(drv->fd);
		drv->fd = -1;
		free(drv->rx_buffer);
		drv->rx_buffer = NULL;
		pthread_mutex_destroy(&drv->tx_lock);
		pthread_mutex_destroy(&drv->state_lock);
		return -1;
	}

	drv->running = 1;
	if (pthread_create(&drv->thread_handle, NULL, tty_rx_thread, drv) != 0) {
		drv->running = 0;
		close(drv->fd);
		drv->fd = -1;
		free(drv->rx_buffer);
		drv->rx_buffer = NULL;
		pthread_mutex_destroy(&drv->tx_lock);
		pthread_mutex_destroy(&drv->state_lock);
		return -1;
	}

	return 0;
}

void tty_driver_close(tty_driver_t *drv)
{
	if (drv == NULL) {
		return;
	}

	pthread_mutex_lock(&drv->state_lock);
	int was_running = drv->running;
	drv->running = 0;
	pthread_mutex_unlock(&drv->state_lock);

	if (drv->fd >= 0) {
		close(drv->fd);
		drv->fd = -1;
	}

	if (was_running) {
		pthread_join(drv->thread_handle, NULL);
	}

	if (drv->rx_buffer != NULL) {
		free(drv->rx_buffer);
		drv->rx_buffer = NULL;
	}

	pthread_mutex_destroy(&drv->tx_lock);
	pthread_mutex_destroy(&drv->state_lock);
}

ssize_t tty_driver_send(tty_driver_t *drv, const void *data, size_t len)
{
	if (drv == NULL || data == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (len == 0) {
		return 0;
	}

	pthread_mutex_lock(&drv->tx_lock);

	size_t sent = 0;
	const uint8_t *p = (const uint8_t *)data;
	while (sent < len) {
		ssize_t n = write(drv->fd, p + sent, len - sent);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				struct pollfd pfd;
				pfd.fd = drv->fd;
				pfd.events = POLLOUT;
				pfd.revents = 0;
				if (poll(&pfd, 1, 1000) <= 0) {
					pthread_mutex_unlock(&drv->tx_lock);
					return -1;
				}
				continue;
			}
			pthread_mutex_unlock(&drv->tx_lock);
			return -1;
		}

		sent += (size_t)n;
	}

	pthread_mutex_unlock(&drv->tx_lock);
	return (ssize_t)sent;
}

int tty_driver_set_rx_callback(tty_driver_t *drv, tty_rx_callback_t cb, void *user_ctx)
{
	if (drv == NULL) {
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&drv->state_lock);
	drv->callback = cb;
	drv->callback_user_ctx = user_ctx;
	pthread_mutex_unlock(&drv->state_lock);

	return 0;
}
