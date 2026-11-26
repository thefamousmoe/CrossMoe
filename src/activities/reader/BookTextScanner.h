#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Streams one chapter's XHTML and finds whole-word, case-insensitive matches
// of a query in its visible text. Memory use is fixed: a small context ring
// plus the hits themselves, never the chapter.
class BookTextScanner final : public Print {
 public:
  struct Hit {
    uint16_t spineIndex = 0;
    uint16_t paragraphIndex = UINT16_MAX;
    uint32_t byteOffset = 0;  // position in the chapter source, for a progress fallback
    float chapterProgress = 0.0f;
    uint8_t bookPercent = 0;
    std::string snippet;
  };

  static constexpr size_t MAX_HITS = 200;
  static constexpr size_t MAX_QUERY_BYTES = 64;

  // Returns false when the query is empty or too long to search.
  bool setQuery(const std::string& query);
  void beginChapter(uint16_t spineIndex);
  // Finalizes snippets still waiting for trailing context.
  void endChapter();

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  bool limitReached() const { return hits.size() >= MAX_HITS; }
  uint32_t chapterBytes() const { return bytesSeen; }

  std::vector<Hit> hits;

 private:
  static constexpr size_t RING = 256;
  static constexpr size_t SNIPPET_BEFORE = 36;
  static constexpr size_t SNIPPET_AFTER = 44;
  static constexpr size_t MAX_PENDING = 4;

  enum class Mode : uint8_t { Text, Tag, Entity };

  void feed(uint8_t c);
  void handleTagEnd();
  void emit(uint8_t c);
  void emitBoundary();
  void flushEntity(bool terminated);
  void confirmCandidate(bool nextIsWordChar);
  void appendTrailing(uint8_t c);
  void closePending();
  uint8_t ringAt(uint32_t absolute) const { return ring[absolute % RING]; }

  std::string query;  // ASCII-lowercased
  std::vector<uint8_t> failure;

  // Per chapter.
  uint16_t spine = 0;
  uint32_t bytesSeen = 0;
  uint16_t paragraph = 0;
  Mode mode = Mode::Text;
  char tagName[12] = {};
  uint8_t tagNameLen = 0;
  bool tagNameDone = false;
  bool tagClosing = false;
  bool tagSelfClosing = false;
  char skipTag[8] = {};  // non-empty while inside head/script/style
  char entity[12] = {};
  uint8_t entityLen = 0;
  bool lastWasSpace = true;
  bool heldC2 = false;  // first byte of a possible raw soft hyphen

  // Emitted visible text, as a ring of the most recent bytes.
  uint8_t ring[RING] = {};
  uint32_t emitted = 0;
  uint32_t blockStart = 0;  // emitted count at the current block's start
  size_t matched = 0;       // KMP state
  bool candidate = false;   // a match ended on the previous byte; waiting for the next byte
  uint32_t candidateStart = 0;
  uint32_t candidateOffset = 0;
  uint16_t candidateParagraph = 0;

  struct Pending {
    size_t hitIndex;
    size_t remaining;
  };
  Pending pending[MAX_PENDING] = {};
  size_t pendingCount = 0;
  uint32_t lastYieldMs = 0;
};
