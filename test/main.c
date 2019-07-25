#include "acutest.h"

void test_ape(void);
void test_buffer(void);

TEST_LIST = {
    {"ape", test_ape},
    {"buffer", test_buffer},
    {0}
};
