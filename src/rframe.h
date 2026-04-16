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

typedef void (*rframe_rx_payload_handler_t)(rframe_payload_t payload, void *user_ctx);

tty_driver_t *rframe_init(char *device_path,
                          rframe_rx_payload_handler_t rx_payload_handler,
                          void *user_ctx);
uint8_t rframe_close(tty_driver_t *drv);

uint8_t rframe_send_payload(tty_driver_t *drv, rframe_payload_t *payload_p);

#endif  // __RPMSG_H