#ifndef __RPMSG_H
#define __RPMSG_H

#include <stdint.h>

#include "tty_driver.h"

typedef struct {
    uint16_t header;
    uint16_t cmd;
    uint8_t data_length;
    uint8_t data[256];
} rframe_payload_t;

tty_driver_t *rframe_init(char *device_path);
uint8_t rframe_close(tty_driver_t *drv);

uint8_t rframe_send_payload(tty_driver_t *drv, rframe_payload_t *payload_p);
void on_rpmsg_rx(const uint8_t *data, size_t len, tty_rx_reason_t reason, void *user_ctx);

#endif  // __RPMSG_H