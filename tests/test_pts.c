#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pts.h"
#include "unity.h"

static pts_session_t *session;
static const char *device_path;
static int slave_fd;

void setUp(void)
{
    session = NULL;
    device_path = NULL;
    slave_fd = -1;

    TEST_ASSERT_EQUAL(0, pts_init(&session, &device_path, NULL));
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_NOT_NULL(device_path);

    slave_fd = open(device_path, O_RDWR | O_NOCTTY);
    TEST_ASSERT_NOT_EQUAL(-1, slave_fd);
}

void tearDown(void)
{
    if (slave_fd >= 0) {
        close(slave_fd);
        slave_fd = -1;
    }

    pts_release(session);
    session = NULL;
}

void test_pts_collects_slave_writes(void)
{
    const uint8_t expected[] = {0x41, 0x42, 0x43, 0x44};

    TEST_ASSERT_EQUAL((int)sizeof(expected), (int)write(slave_fd, expected, sizeof(expected)));
    usleep(100000);

    uint8_t *data = NULL;
    size_t len = 0;
    TEST_ASSERT_EQUAL(0, pts_take_rx_data(session, &data, &len));
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL((int)sizeof(expected), (int)len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, len);

    free(data);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pts_collects_slave_writes);
    return UNITY_END();
}