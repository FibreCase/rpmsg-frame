
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
static rframe_payload_t last_payload;
static int payload_received;

static void on_rframe_payload(rframe_payload_t payload, void *user_ctx)
{
    (void)user_ctx;
    last_payload = payload;
    payload_received = 1;
}

void setUp(void)
{
    drv = NULL;
    session = NULL;
    device_path = NULL;
    payload_received = 0;
    memset(&last_payload, 0, sizeof(last_payload));

    TEST_ASSERT_EQUAL(0, pts_init(&session, &device_path, NULL));
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_NOT_NULL(device_path);

    drv = rframe_init((char *)device_path, on_rframe_payload, NULL);
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

void test_rframe_receive_payload_matches_pts_tx(void)
{
    // Create test data
    uint16_t cmd = 0x0304;
    uint8_t data[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t data_length = sizeof(data);

    // Construct little-endian frame bytes: [header_lo, header_hi, cmd_lo, cmd_hi, len, data...]
    uint8_t frame[] = {
        0x55, 0xAA,
        cmd & 0xFF, (cmd >> 8) & 0xFF,
        data_length,
        0xCA, 0xFE, 0xBA, 0xBE
    };
    size_t frame_len = sizeof(frame);

    // Reset payload state for this test
    payload_received = 0;
    memset(&last_payload, 0, sizeof(last_payload));

    // Send the frame via PTS (simulating reception from the device)
    TEST_ASSERT_EQUAL(0, pts_send_tx_data(session, frame, frame_len));

    // Wait for the callback to be invoked
    usleep(100000);

    // Verify payload was received through the callback
    TEST_ASSERT_EQUAL_INT(1, payload_received);
    TEST_ASSERT_EQUAL_UINT16(0xAA55, last_payload.header);
    TEST_ASSERT_EQUAL_UINT16(cmd, last_payload.cmd);
    TEST_ASSERT_EQUAL_UINT8(data_length, last_payload.data_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, last_payload.data, data_length);
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

    uint8_t expected[] = {
        (uint8_t)(expected_header & 0xFF),
        (uint8_t)((expected_header >> 8) & 0xFF),
        (uint8_t)(payload.cmd & 0xFF),
        (uint8_t)((payload.cmd >> 8) & 0xFF),
        payload.data_length,
        0xDE,
        0xAD,
        0xBE,
        0xEF,
    };

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, rx_data, rx_len);
    free(rx_data);
}

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rframe_receive_payload_matches_pts_tx);
    RUN_TEST(test_rframe_send_payload_matches_pts_rx);
    return UNITY_END();
}