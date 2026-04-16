
#include "rframe.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "tty_driver.h"

void on_rpmsg_rx(const uint8_t *data, size_t len, tty_rx_reason_t reason, void *user_ctx) {
    (void)user_ctx;
    printf("RX callback: len=%zu, reason=%s\n", len,
           reason == TTY_RX_REASON_IDLE ? "IDLE" : "BUFFER_FULL");
}

tty_driver_t *rframe_init(char *device_path) {
    if (device_path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    tty_driver_t *drv = (tty_driver_t *)malloc(sizeof(*drv));
    if (drv == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    tty_driver_config_t cfg = {
        .device_path     = device_path,
        .rx_buffer_size  = 1024,
        .idle_timeout_ms = 20,
    };

    if (tty_driver_open(drv, &cfg) != 0) {
        perror("tty_driver_open");
        free(drv);
        return NULL;
    }

    if (tty_driver_set_rx_callback(drv, on_rpmsg_rx, NULL) != 0) {
        perror("tty_driver_set_rx_callback");
        tty_driver_close(drv);
        free(drv);
        return NULL;
    }

    return drv;
}

uint8_t rframe_close(tty_driver_t *drv) {
    if (drv == NULL) {
        errno = EINVAL;
        return 1;
    }
    tty_driver_close(drv);
    free(drv);
    return 0;
}

uint8_t rframe_send_payload(tty_driver_t *drv, rframe_payload_t *payload_p) {
    if (drv == NULL || payload_p == NULL || payload_p->data_length > sizeof(payload_p->data)) {
        errno = EINVAL;
        return 1;
    }

    payload_p->header    = 0xAA55;
    uint16_t payload_len = sizeof(payload_p->header) + sizeof(payload_p->cmd) +
                           sizeof(payload_p->data_length) + payload_p->data_length;

    if (tty_driver_send(drv, payload_p, payload_len) < 0) {
        perror("tty_driver_send");
        return 1;
    }
    return 0;
}
