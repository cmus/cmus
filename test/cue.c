#define TEST_NO_MAIN
#include "acutest.h"
#include "../cue.h"

#include <stdbool.h>
#include <string.h>

static bool eq_str(const char * str1, const char *str2)
{
  const size_t threshold = 1024;
  return strncmp(str1, str2, threshold) == 0;
}

static void test_cue_blank(void)
{
  const char *src = "";
  struct cue_sheet *sheet = cue_parse(src, 0);
  TEST_CHECK(sheet == NULL);
}

static void test_cue_parse(void)
{
  struct cue_sheet *sheet = cue_from_file("test/fixtures/cue_normal.cue");
  if (!TEST_CHECK(sheet != NULL)) return;

  TEST_CHECK(sheet->num_tracks == 5);
  TEST_CHECK(sheet->track_base == 1);

  {
    const char *performer = "Potato Masters";
    const char *songwriter = "potato songwriter";
    const char *title = "Who Potato? That Potato!";
    const char *genre = "Potatofunk";
    const char *date = "1988";
    const char *compilation = "TRUE";

    TEST_CHECK(eq_str(performer, sheet->meta.performer));
    TEST_CHECK(eq_str(songwriter, sheet->meta.songwriter));
    TEST_CHECK(eq_str(title, sheet->meta.title));
    TEST_CHECK(eq_str(genre, sheet->meta.genre));
    TEST_CHECK(eq_str(date, sheet->meta.date));
    TEST_CHECK(eq_str(compilation, sheet->meta.compilation));

    // TODO: I'm not sure what a comment looks like for a cue file
    // TEST_CHECK(eq_str(comment, sheet->meta.comment));
  }

  {
    struct track_info {
	const char *performer;
	const char *songwriter;
	const char *title;
	const char *genre;
	const char *date;
	const char *comment;
	const char *compilation;
    };

    struct track_info expected[] = {
      {"Potato Masters", "", "The Potato Song", "", "", "", ""},
      {"Potato Masters", "", "The Even Better Potato Song", "", "", "", ""},
      {"Potato Masters", "", "The Potato Vacation", "", "", "", ""},
      {"Bob", "", "I Dream of Potato", "", "", "", ""},
      {"Potato Masters", "", "All Potatoes Shall be Mine", "", "", "", ""}
    };

    for (size_t i = 0; i < sheet->num_tracks; ++i) {
      struct cue_track *track = sheet->tracks + i;

      TEST_CHECK(eq_str(track->meta.title, expected[i].title));
      TEST_CHECK(eq_str(track->meta.performer, expected[i].performer));
      // TODO: this will segfault; may need to find a better way to
      // test for this.
      // TEST_CHECK(eq_str(track->meta.songwriter, expected[i].songwriter));
    }
  }

  cue_free(sheet);
}

static void test_cue_parse_bad_file(void)
{
  struct cue_sheet *sheet =
    cue_from_file("test/fixtures/cue_bad_file.cue");
  TEST_CHECK(sheet == NULL);
}

void test_cue(void)
{
  test_cue_blank();
  test_cue_parse();
  test_cue_parse_bad_file();
}
