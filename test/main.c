#include "acutest.h"

void test_ape(void);
void test_buffer(void);
void test_cue(void);
void test_utils(void);

TEST_LIST = {
    {"ape", test_ape},
    {"buffer", test_buffer},
    {"cue", test_cue},
    {"utils", test_utils},
    {0}
};
