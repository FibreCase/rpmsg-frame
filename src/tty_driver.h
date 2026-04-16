#ifndef TTY_DRIVER_H
#define TTY_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tty_driver tty_driver_t;

typedef enum tty_rx_reason {
    TTY_RX_REASON_IDLE = 0,
    TTY_RX_REASON_BUFFER_FULL = 1
} tty_rx_reason_t;

typedef void (*tty_rx_callback_t)(const uint8_t *data,
                                  size_t len,
                                  tty_rx_reason_t reason,
                                  void *user_ctx);

typedef struct tty_driver_config {
    const char *device_path;
    size_t rx_buffer_size;
    unsigned int idle_timeout_ms;
} tty_driver_config_t;

/* Open device and start background RX loop. */
int tty_driver_open(tty_driver_t *drv, const tty_driver_config_t *cfg);

/* Close device and stop background RX loop. */
void tty_driver_close(tty_driver_t *drv);

/* Interface 1: Send arbitrary-length payload. */
ssize_t tty_driver_send(tty_driver_t *drv, const void *data, size_t len);

/* Interface 2: Register callback fired on RX idle or buffer full. */
int tty_driver_set_rx_callback(tty_driver_t *drv,
                               tty_rx_callback_t cb,
                               void *user_ctx);

/* Opaque storage for the driver instance. */
struct tty_driver {
    int fd;
    int running;
    unsigned int idle_timeout_ms;
    size_t rx_capacity;
    size_t rx_length;
    uint8_t *rx_buffer;
    tty_rx_callback_t callback;
    void *callback_user_ctx;
    pthread_t thread_handle;
    pthread_mutex_t state_lock;
    pthread_mutex_t tx_lock;
};

#ifdef __cplusplus
}
#endif

#endif