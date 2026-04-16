
#include "rframe.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tty_driver.h"

static rframe_rx_payload_handler_t g_rx_payload_handler;
static void *g_rx_payload_handler_user_ctx;

static void rpmsg_rx_payload_dispatch(rframe_payload_t payload) {
    if (g_rx_payload_handler != NULL) {
        g_rx_payload_handler(payload, g_rx_payload_handler_user_ctx);
    }
}

static void rpmsg_rx_fsm(uint8_t byte) {
    static enum { WAIT_HEADER, WAIT_CMD, WAIT_LENGTH, WAIT_DATA } state = WAIT_HEADER;

    static uint16_t header;
    static uint16_t cmd;
    static uint8_t data_length;
    static uint8_t data_buffer[256];
    static size_t data_index;
    static uint8_t field_bytes;

    switch (state) {
        case WAIT_HEADER:
            header = (header << 8) | byte;
            field_bytes++;
            if (field_bytes == 2 && header == 0xAA55) {
                state       = WAIT_CMD;
                cmd         = 0;
                field_bytes = 0;
            } else if (field_bytes == 2) {
                field_bytes = 1;
            }
            break;
        case WAIT_CMD:
            cmd = (cmd << 8) | byte;
            field_bytes++;
            if (field_bytes == 2) {
                state       = WAIT_LENGTH;
                field_bytes = 0;
            }
            break;
        case WAIT_LENGTH:
            data_length = byte;
            if (data_length > sizeof(data_buffer)) {
                // Invalid length, reset FSM
                state       = WAIT_HEADER;
                header      = 0;
                field_bytes = 0;
            } else if (data_length == 0) {
                // No data, process command immediately
                rframe_payload_t ret_payload = {.header = header, .cmd = cmd, .data_length = 0};
                rpmsg_rx_payload_dispatch(ret_payload);
                state  = WAIT_HEADER;
                header = 0;
            } else {
                state      = WAIT_DATA;
                data_index = 0;
            }
            break;
        case WAIT_DATA:
            data_buffer[data_index++] = byte;
            if (data_index >= data_length) {
                // Full payload received, process it
                rframe_payload_t ret_payload = {
                    .header = header, .cmd = cmd, .data_length = data_length};
                memcpy(ret_payload.data, data_buffer, data_length);
                rpmsg_rx_payload_dispatch(ret_payload);
                state       = WAIT_HEADER;
                header      = 0;
                field_bytes = 0;
            }
            break;
    }
}

static void rpmsg_rx_handler(int length, const uint8_t *data) {
    for (size_t i = 0; i < length; i++) {
        rpmsg_rx_fsm(data[i]);
    }
}

static void on_rpmsg_rx(const uint8_t *data, size_t len, tty_rx_reason_t reason, void *user_ctx) {
    (void)user_ctx;
    printf("RX callback: len=%zu, reason=%s\n", len,
           reason == TTY_RX_REASON_IDLE ? "IDLE" : "BUFFER_FULL");

    rpmsg_rx_handler(len, data);
}

tty_driver_t *rframe_init(char *device_path,
                          rframe_rx_payload_handler_t rx_payload_handler,
                          void *user_ctx) {
    if (device_path == NULL || rx_payload_handler == NULL) {
        errno = EINVAL;
        return NULL;
    }

    g_rx_payload_handler = rx_payload_handler;
    g_rx_payload_handler_user_ctx = user_ctx;

    tty_driver_t *drv = (tty_driver_t *)malloc(sizeof(*drv));
    if (drv == NULL) {
        errno = ENOMEM;
        g_rx_payload_handler = NULL;
        g_rx_payload_handler_user_ctx = NULL;
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
        g_rx_payload_handler = NULL;
        g_rx_payload_handler_user_ctx = NULL;
        return NULL;
    }

    if (tty_driver_set_rx_callback(drv, on_rpmsg_rx, NULL) != 0) {
        perror("tty_driver_set_rx_callback");
        tty_driver_close(drv);
        free(drv);
        g_rx_payload_handler = NULL;
        g_rx_payload_handler_user_ctx = NULL;
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
