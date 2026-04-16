
#include <stdio.h>
#include <stdlib.h>

#include "rframe.h"

int main() {
    tty_driver_t* drv;
    if (!(drv = rframe_init("/dev/pts/8"))) {
        fprintf(stderr, "Failed to initialize rframe\n");
        return -1;
    }

    rframe_payload_t payload = {.cmd = 0x01, .data_length = 1, .data = {0x42}};

    rframe_send_payload(drv, &payload);

    getchar();

    rframe_close(drv);

    return 0;
}