#pragma once

#include <cstdint>
#include <vector>

// Compact copy of one match, kept by the reader to step between matches.
struct BookSearchHitRef {
  uint16_t spineIndex;
  uint16_t paragraphIndex;
  float chapterProgress;
};

// Filled when the user picks a result: every match in book order, and which one was chosen.
struct BookSearchSelection {
  std::vector<BookSearchHitRef> hits;
  int selected = -1;
};
