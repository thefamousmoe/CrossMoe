#pragma once

#include <cstdint>
#include <vector>

// Compact copy of one match, kept by the reader to step between matches.
struct BookSearchHitRef {
  uint16_t spineIndex;
  uint16_t paragraphIndex;
  float chapterProgress;
  // Fingerprints of the two words before and after the match (0 = none), used
  // to find the exact page when the word appears more than once nearby.
  uint16_t context[4];
};

// ~16 bytes per match, at most 200 matches.
// Filled when the user picks a result: every match in book order, and which one was chosen.
struct BookSearchSelection {
  std::vector<BookSearchHitRef> hits;
  int selected = -1;
};
