#define TEST_NO_MAIN
#include "acutest.h"
#include "../buffer.h"

#include <stdbool.h>
#include <stdio.h>

static bool test_buffer_init(void)
{
  buffer_init();
  char *pos = NULL;

  const int ret = buffer_get_rpos(&pos);
  TEST_CHECK(ret == 0);
  TEST_CHECK(pos == NULL);

  return true;
}

static bool test_buffer_free(void)
{
  buffer_free();
  return true;
}

static bool test_buffer_get_rpos(void)
{
  char *pos = NULL;

  const int ret = buffer_get_rpos(&pos);
  TEST_CHECK(ret == 0);

  return true;
}

static bool test_buffer_get_wpos(void)
{
  char *pos = NULL;

  const int ret = buffer_get_wpos(&pos);
  TEST_CHECK(ret == CHUNK_SIZE);

  return true;
}

static bool test_buffer_consume(void)
{
  const int consume = 5;
  const int ret = buffer_fill(consume);
  TEST_CHECK(ret == 0);
  return true;
}

static bool test_buffer_fill(void)
{
  const int fill = 5;
  const int ret = buffer_fill(fill);
  TEST_CHECK(ret == 0);

  char *rpos = NULL, *wpos = NULL;
  (void) buffer_get_wpos(&wpos);
  (void) buffer_get_rpos(&rpos);
  TEST_CHECK(wpos == 0);
  TEST_CHECK(rpos == 0);

  return true;
}

static bool test_buffer_reset(void)
{
  buffer_reset();
  return true;
}

static bool test_buffer_get_filled_chunks(void)
{
  {
    const int ret = buffer_get_filled_chunks();
    TEST_CHECK(ret == 0);
  }

  {
    buffer_fill(10);
    const int ret = buffer_get_filled_chunks();
    TEST_CHECK(ret == 0);
  }

  {
    buffer_fill(20);
    const int ret = buffer_get_filled_chunks();
    TEST_CHECK(ret == 0);
  }

  return true;
}

void test_buffer(void)
{
  TEST_CHECK(test_buffer_init());
  TEST_CHECK(test_buffer_get_rpos());
  TEST_CHECK(test_buffer_get_wpos());
  TEST_CHECK(test_buffer_consume());
  TEST_CHECK(test_buffer_fill());
  TEST_CHECK(test_buffer_get_filled_chunks());
  TEST_CHECK(test_buffer_reset());
  TEST_CHECK(test_buffer_free());
}
