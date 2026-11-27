#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BookTextScanner.h"

// Compact copy of one match, kept by the reader to step between matches.
struct BookSearchHitRef {
  uint16_t spineIndex;
  uint16_t paragraphIndex;
  float chapterProgress;
  // Fingerprints of the two words before and after the match (0 = none), used
  // to find the exact page when the word appears more than once nearby.
  uint16_t context[4];
};

struct BookSearchChapterName {
  uint16_t spineIndex;
  std::string title;
};

// ~16 bytes per match, at most 200 matches.
// Filled when the user picks a result: every match in book order, and which one was chosen.
struct BookSearchSelection {
  std::vector<BookSearchHitRef> hits;
  int selected = -1;

  // The results list itself (snippets and chapter names), handed over when a
  // result is picked so Back can show the same list again without searching
  // the book a second time. Typically a few KB; about 30 KB at the 200-match
  // limit. Freed when search mode ends.
  std::vector<BookTextScanner::Hit> listHits;
  std::vector<BookSearchChapterName> listChapters;

  void releaseList() {
    std::vector<BookTextScanner::Hit>().swap(listHits);
    std::vector<BookSearchChapterName>().swap(listChapters);
  }
};
