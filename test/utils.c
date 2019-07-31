#define TEST_NO_MAIN
#include "acutest.h"
#include "../utils.h"

#include <stdbool.h>

static void test_min_i(void)
{
  const long min = 1;
  const long max = 2;
  TEST_CHECK(min == min_i(min, max));
}

static void test_min_u(void)
{
  const unsigned long min = 1;
  const unsigned long max = 2;
  TEST_CHECK(min == min_u(min, max));
}

static void test_max_i(void)
{
  const long min = 1;
  const long max = 2;
  TEST_CHECK(max == max_i(min, max));
}

static void test_clamp(void)
{
  const int a = 1;
  const int b = 2;
  const int c = 3;

  TEST_CHECK(b == clamp(b, a, c));
  TEST_CHECK(b == clamp(a, b, c));
  TEST_CHECK(b == clamp(c, a, b));
}

static void test_scale_from_percentage(void)
{
  const int a = -10;
  const int b = 20;
  const int c = 30;

  TEST_CHECK(scale_from_percentage(a, b) == -2);
  TEST_CHECK(scale_from_percentage(b, c) == 6);
  TEST_CHECK(scale_from_percentage(0, b) == 0);
}

static void test_scale_to_percentage(void)
{
  const int a = 10;
  const int b = 100;
  const int c = -100;

  TEST_CHECK(scale_to_percentage(a, c) == 100);
  TEST_CHECK(scale_to_percentage(a, 0) == 100);
  TEST_CHECK(scale_to_percentage(a, b) == 10);
  TEST_CHECK(scale_to_percentage(0, b) == 0);
}

static void test_str_to_int(void)
{
  {
    const char *num = "1234";
    const long int expected = 1234;
    long int val = 0;

    const int ret = str_to_int(num, &val);
    TEST_CHECK(ret == 0);
    TEST_CHECK(val == expected);
  }
  {
    const char *bad = "potato";
    long int val = 0;

    const int ret = str_to_int(bad, &val);
    TEST_CHECK(ret == -1);
  }
  {
    const char *bad = "1234potato";
    long int val = 0;

    const int ret = str_to_int(bad, &val);
    TEST_CHECK(ret == -1);
  }
  {
    const char *bad = "potato1234";
    long int val = 0;

    const int ret = str_to_int(bad, &val);
    TEST_CHECK(ret == -1);
  }
}

static void test_strcmp0(void)
{
  {
    const char *str1 = "abc";
    const char *str2 = "abc";
    TEST_CHECK(strcmp0(str1, str2) == 0);
  }
  {
    const char *str1 = "abc";
    const char *str2 = "cba";
    TEST_CHECK(strcmp0(str1, str2) == -1);
  }
  {
    const char *str1 = "abc";
    const char *str2 = NULL;
    TEST_CHECK(strcmp0(str1, str2) == 1);
  }
  {
    const char *str1 = NULL;
    const char *str2 = "abc";
    TEST_CHECK(strcmp0(str1, str2) == -1);

    const char *str3 = NULL;
    TEST_CHECK(strcmp0(str1, str3) == 0);
  }
}

static void test_is_space(void)
{
  TEST_CHECK(is_space(' '));
  TEST_CHECK(is_space('\t'));

  TEST_CHECK(!is_space('\v'));
  TEST_CHECK(!is_space('\r'));
  TEST_CHECK(!is_space('\n'));
  TEST_CHECK(!is_space('a'));
  TEST_CHECK(!is_space('Z'));
}

static void test_ends_with(void)
{
  const char *str = "potatohero";
  const char *ending = "hero";
  const char *alternate = "villain";

  TEST_CHECK(ends_with(str, str));
  TEST_CHECK(ends_with(str, ending));
  TEST_CHECK(!ends_with(str, alternate));
}

static void test_strip_trailing_spaces(void)
{
  char *not_trailing = strdup("something here");
  const char *expected_not_trailing = "something here";

  char *trailing = strdup("hello there    ");
  char *trailing_tab = strdup("hello there  \t  ");
  const char *expected_trailing = "hello there";

  strip_trailing_spaces(not_trailing);
  strip_trailing_spaces(trailing);
  strip_trailing_spaces(trailing_tab);

  TEST_CHECK(strcmp0(not_trailing, expected_not_trailing) == 0);
  TEST_CHECK(strcmp0(trailing, expected_trailing) == 0);
  TEST_CHECK(strcmp0(trailing_tab, expected_trailing) == 0);

  free(not_trailing);
  free(trailing);
  free(trailing_tab);
}

static void test_is_http_url(void)
{
  TEST_CHECK(is_http_url("http://")); // technically should probably fail
  TEST_CHECK(is_http_url("http://www.cmus.mus"));
  TEST_CHECK(!is_http_url("www.cmus.mus"));
  TEST_CHECK(!is_http_url(""));
  TEST_CHECK(!is_http_url("potato"));
  TEST_CHECK(!is_http_url("htp://www.cmus.mus"));
  TEST_CHECK(!is_http_url("htt://www.cmus.mus"));
}

static void test_is_cdda_url(void)
{
  TEST_CHECK(is_cdda_url("cdda://")); // technically should probably fail
  TEST_CHECK(is_cdda_url("cdda://www.cmus.mus"));
  TEST_CHECK(!is_cdda_url("cda://www.cmus.mus"));
  TEST_CHECK(!is_cdda_url("http://www.cmus.mus"));
}

static void test_is_cue_url(void)
{
  TEST_CHECK(is_cue_url("cue://")); // technically should probably fail
  TEST_CHECK(is_cue_url("cue://someplace"));
  TEST_CHECK(is_cue_url("cue://some/place/else"));
  TEST_CHECK(!is_cue_url("ce://some/place/else"));
}

static void test_is_url(void)
{
  TEST_CHECK(is_url("cue://"));
  TEST_CHECK(is_url("cdda://"));
  TEST_CHECK(is_url("http://"));
}

static void test_strscpy(void)
{
  const int fail = -1;

  { /* TODO: equal length strings fail -- is this normal? */
    const char *expected = "xxxxaaa";
    char *str1 = strdup("xxxxxxx");
    char *str2 = strdup(expected);

    const ssize_t ret = strscpy(str1, str2, strlen(str1) + 1);
    TEST_CHECK(ret != fail);
    TEST_CHECK(strcmp0(str1, expected) == 0);

    free(str1);
    free(str2);
  }

  {
    const char *expected = " aaa";
    char *str1 = strdup("xxxxxxxx");
    char *str2 = strdup(expected);

    const ssize_t ret = strscpy(str1, str2, strlen(str1));
    TEST_CHECK(ret != fail);
    TEST_CHECK(strcmp0(str1, expected) == 0);

    free(str1);
    free(str2);
  }

  { /* should fail on bigger src string than dest */
    char *str1 = strdup("xxx");
    char *str2 = strdup("    aaa");

    const ssize_t ret = strscpy(str1, str2, strlen(str1));
    TEST_CHECK(ret == fail);

    free(str1);
    free(str2);
  }
}

void test_utils(void)
{
  test_min_i();
  test_min_u();
  test_max_i();
  test_clamp();
  test_scale_from_percentage();
  test_scale_to_percentage();
  test_str_to_int();
  test_strcmp0();
  test_is_space();
  test_ends_with();
  test_strip_trailing_spaces();
  test_is_http_url();
  test_is_cdda_url();
  test_is_cue_url();
  test_is_url();
  test_strscpy();
}
