#include "unity.h"
#include "tinyobsw/crc16.h"

void setUp(void) {}
void tearDown(void) {}

static void test_standard_check_value(void)
{
    const uint8_t msg[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, obsw_crc16(msg, 9));
}

static void test_empty_input_returns_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, obsw_crc16(NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_check_value);
    RUN_TEST(test_empty_input_returns_init);
    return UNITY_END();
}
