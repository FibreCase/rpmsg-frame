
#include "tty_driver.h"
#include "rframe.h"
#include "unity.h"

tty_driver_t* drv;

void setUp(void) {
    if (!(drv = rframe_init("/dev/pts/8"))) {
        fprintf(stderr, "Failed to initialize rframe\n");
        return;
    }

    rframe_payload_t payload = {.cmd = 0x01, .data_length = 1, .data = {0x42}};
}

void tearDown(void) {
    rframe_close(drv);
}

void test_function_add(void) { TEST_ASSERT_EQUAL(0, 0); }

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_function_add);
    return UNITY_END();
}