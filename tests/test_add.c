
#include "add.h"
#include "unity.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_function_add(void) { TEST_ASSERT_EQUAL(3, add(1, 2)); }

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_function_add);
    return UNITY_END();
}