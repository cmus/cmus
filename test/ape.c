#define TEST_NO_MAIN
#include "acutest.h"
#include "../ape.h"

#include <stdbool.h>
#include <stdio.h>

static bool test_against_blank_ape(void)
{
  struct apetag data = {0};
  struct ape_header header = {0};
  data.header = header;
  return ape_read_tags(&data, 0, 0) == -1;
}

static bool test_bad_tags(void)
{
  struct apetag data = {0};
  struct ape_header header = {0};
  data.header = header;

  const int fail = -1;

  data.header.size = 1024 * 1024 + 1;
  if (ape_read_tags(&data, 0, 0) == fail) {
    TEST_MSG("should fail on exceeding header size");
    return false;
  }

  data.header.size = (1024 * 1024) - 1;
  if (ape_read_tags(&data, 0, 0) != fail) {
    TEST_MSG("should succeed on ok header size");
    return false;
  }

  return true;
}

static bool test_ape_read_tags(void)
{
  struct apetag data = {0};
  struct ape_header header = {0};

  data.header = header;
  data.buf = strdup("the song the song the song data");

  ape_read_tags(&data, 0, 0);

  ape_free(&data);
  return true;
}

static bool test_ape_get_comment_bad(void)
{
  struct apetag data = {0};
  struct ape_header header = {0};
  data.header = header;

  data.pos = 2;
  data.header.size = 1;

  // is pos >= size
  char *ret = ape_get_comment(&data, NULL);
  TEST_CHECK(ret == NULL);

  return true;
}

static bool test_ape_get_comment(void) {
  const bool implement_me = false;
  TEST_CHECK(implement_me && false);
  return false;
}

void test_ape(void) {
  TEST_CHECK(test_against_blank_ape());
  TEST_CHECK(test_bad_tags());
  TEST_CHECK(test_ape_read_tags());
  TEST_CHECK(test_ape_get_comment_bad());
  TEST_CHECK(test_ape_get_comment());
}
