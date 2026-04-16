
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pts.h"
#include "rframe.h"
#include "tty_driver.h"
#include "unity.h"

static tty_driver_t *drv;
static pts_session_t *session;
static const char *device_path;

void setUp(void)
{
    drv = NULL;
    session = NULL;
    device_path = NULL;

    TEST_ASSERT_EQUAL(0, pts_init(&session, &device_path, NULL));
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_NOT_NULL(device_path);

    drv = rframe_init((char *)device_path);
    TEST_ASSERT_NOT_NULL(drv);
}

void tearDown(void)
{
    if (drv != NULL) {
        TEST_ASSERT_EQUAL_UINT8(0, rframe_close(drv));
        drv = NULL;
    }

    pts_release(session);
    session = NULL;
}

void test_rframe_send_payload_matches_pts_rx(void)
{
    rframe_payload_t payload = {
        .cmd = 0x0102,
        .data_length = 4,
        .data = {0xDE, 0xAD, 0xBE, 0xEF},
    };

    TEST_ASSERT_EQUAL_UINT8(0, rframe_send_payload(drv, &payload));

    usleep(100000);

    uint8_t *rx_data = NULL;
    size_t rx_len = 0;
    TEST_ASSERT_EQUAL(0, pts_take_rx_data(session, &rx_data, &rx_len));
    TEST_ASSERT_NOT_NULL(rx_data);

    uint16_t expected_header = 0xAA55;
    size_t expected_len = sizeof(expected_header) + sizeof(payload.cmd) +
                          sizeof(payload.data_length) + payload.data_length;
    TEST_ASSERT_EQUAL(expected_len, rx_len);

    uint8_t expected[2 + 2 + 1 + 4] = {0};
    size_t offset = 0;
    memcpy(expected + offset, &expected_header, sizeof(expected_header));
    offset += sizeof(expected_header);
    memcpy(expected + offset, &payload.cmd, sizeof(payload.cmd));
    offset += sizeof(payload.cmd);
    expected[offset] = payload.data_length;
    offset += sizeof(payload.data_length);
    memcpy(expected + offset, payload.data, payload.data_length);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, rx_data, rx_len);
    free(rx_data);
}

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rframe_send_payload_matches_pts_rx);
    return UNITY_END();
}