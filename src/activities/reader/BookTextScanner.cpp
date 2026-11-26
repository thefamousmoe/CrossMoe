#include "BookTextScanner.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* ELLIPSIS = "\xE2\x80\xA6";

inline uint8_t foldAscii(const uint8_t c) { return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c + 32) : c; }

// Letters and digits continue a word. Bytes of multi-byte UTF-8 characters
// count as boundaries so "Vin’s" (curly apostrophe) still matches "Vin".
inline bool isWordByte(const uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline bool isTag(const char* name, const char* expected) { return std::strcmp(name, expected) == 0; }

bool isBlockTag(const char* name) {
  static constexpr const char* blocks[] = {"p",  "div", "br", "li", "h1", "h2", "h3",         "h4",      "h5",
                                           "h6", "tr",  "td", "th", "dd", "dt", "blockquote", "section", "hr"};
  for (const char* block : blocks) {
    if (isTag(name, block)) return true;
  }
  return false;
}

// Named entities common in ebooks (punctuation and Latin-1 letters).
const char* namedEntity(const char* name) {
  static constexpr struct {
    const char* name;
    const char* utf8;
  } table[] = {
      {"rsquo", "\xE2\x80\x99"}, {"lsquo", "\xE2\x80\x98"}, {"rdquo", "\xE2\x80\x9D"}, {"ldquo", "\xE2\x80\x9C"},
      {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"}, {"hellip", "\xE2\x80\xA6"}, {"copy", "\xC2\xA9"},
      {"eacute", "\xC3\xA9"},     {"egrave", "\xC3\xA8"},     {"ecirc", "\xC3\xAA"},      {"aacute", "\xC3\xA1"},
      {"agrave", "\xC3\xA0"},     {"iacute", "\xC3\xAD"},     {"oacute", "\xC3\xB3"},     {"uacute", "\xC3\xBA"},
      {"ntilde", "\xC3\xB1"},     {"uuml", "\xC3\xBC"},       {"ouml", "\xC3\xB6"},       {"auml", "\xC3\xA4"},
      {"euml", "\xC3\xAB"},       {"iuml", "\xC3\xAF"},       {"ccedil", "\xC3\xA7"},     {"szlig", "\xC3\x9F"},
  };
  for (const auto& entry : table) {
    if (isTag(name, entry.name)) return entry.utf8;
  }
  return nullptr;
}

// Drop a multi-byte UTF-8 character cut off at the end of the string.
void trimIncompleteUtf8Tail(std::string& text) {
  size_t i = text.size();
  size_t continuation = 0;
  while (i > 0 && continuation < 4) {
    const auto byte = static_cast<uint8_t>(text[i - 1]);
    if ((byte & 0xC0) != 0x80) break;
    ++continuation;
    --i;
  }
  if (i == 0) return;
  const auto lead = static_cast<uint8_t>(text[i - 1]);
  size_t expected = 0;
  if ((lead & 0xE0) == 0xC0)
    expected = 1;
  else if ((lead & 0xF0) == 0xE0)
    expected = 2;
  else if ((lead & 0xF8) == 0xF0)
    expected = 3;
  else
    return;  // ASCII or invalid lead: nothing pending
  if (continuation < expected) text.resize(i - 1);
}

void appendUtf8(std::string& out, uint32_t codepoint) {
  if (codepoint < 0x80) {
    out += static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    out += static_cast<char>(0xC0 | (codepoint >> 6));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    out += static_cast<char>(0xE0 | (codepoint >> 12));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x110000) {
    out += static_cast<char>(0xF0 | (codepoint >> 18));
    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}
}  // namespace

// ---------------------------------------------------------------------------
// BookTextScanner
// ---------------------------------------------------------------------------

bool BookTextScanner::setQuery(const std::string& raw) {
  query.clear();
  for (const char ch : raw) query += static_cast<char>(foldAscii(static_cast<uint8_t>(ch)));
  // Trim surrounding whitespace.
  while (!query.empty() && query.back() == ' ') query.pop_back();
  while (!query.empty() && query.front() == ' ') query.erase(query.begin());
  if (query.empty() || query.size() > MAX_QUERY_BYTES) return false;

  // Knuth-Morris-Pratt failure table so the stream is read exactly once.
  failure.assign(query.size(), 0);
  size_t k = 0;
  for (size_t i = 1; i < query.size(); ++i) {
    while (k > 0 && query[i] != query[k]) k = failure[k - 1];
    if (query[i] == query[k]) ++k;
    failure[i] = static_cast<uint8_t>(k);
  }
  return true;
}

void BookTextScanner::beginChapter(const uint16_t spineIndex) {
  spine = spineIndex;
  bytesSeen = 0;
  paragraph = 0;
  mode = Mode::Text;
  tagNameLen = 0;
  tagNameDone = false;
  tagClosing = false;
  tagSelfClosing = false;
  skipTag[0] = '\0';
  entityLen = 0;
  heldC2 = false;
  lastWasSpace = true;
  emitted = 0;
  blockStart = 0;
  matched = 0;
  candidate = false;
  pendingCount = 0;
}

void BookTextScanner::endChapter() {
  if (mode == Mode::Entity) flushEntity(false);
  if (heldC2) {
    heldC2 = false;
    emit(0xC2);
  }
  if (candidate) confirmCandidate(false);
  closePending();
  const float total = static_cast<float>(std::max<uint32_t>(1, bytesSeen));
  for (auto it = hits.rbegin(); it != hits.rend() && it->spineIndex == spine; ++it) {
    it->chapterProgress = std::clamp(static_cast<float>(it->byteOffset) / total, 0.0f, 1.0f);
  }
}

size_t BookTextScanner::write(const uint8_t c) {
  if (limitReached() && pendingCount == 0) return 0;
  feed(c);
  return 1;
}

size_t BookTextScanner::write(const uint8_t* buffer, const size_t size) {
  // Returning less than size asks the reader to stop streaming this chapter.
  if (limitReached() && pendingCount == 0) return 0;
  for (size_t i = 0; i < size; ++i) feed(buffer[i]);
  // Long chapters can take seconds; let the idle task run so the watchdog stays quiet.
  const uint32_t now = millis();
  if (now - lastYieldMs > 40) {
    delay(1);
    lastYieldMs = millis();
  }
  return size;
}

void BookTextScanner::feed(const uint8_t c) {
  ++bytesSeen;
  switch (mode) {
    case Mode::Text:
      if (c == '<') {
        mode = Mode::Tag;
        tagNameLen = 0;
        tagNameDone = false;
        tagClosing = false;
        tagSelfClosing = false;
        return;
      }
      if (c == '&') {
        mode = Mode::Entity;
        entityLen = 0;
        return;
      }
      if (skipTag[0] != '\0') return;
      // A raw soft hyphen (U+00AD, bytes C2 AD) is invisible and must not split words.
      if (heldC2) {
        heldC2 = false;
        if (c == 0xAD) return;
        emit(0xC2);
      }
      if (c == 0xC2) {
        heldC2 = true;
        return;
      }
      emit(c);
      return;

    case Mode::Tag:
      if (c == '>') {
        tagName[tagNameLen] = '\0';
        handleTagEnd();
        mode = Mode::Text;
        return;
      }
      if (!tagNameDone) {
        if (tagNameLen == 0 && c == '/' && !tagClosing) {
          tagClosing = true;
          return;
        }
        if (isWordByte(c)) {
          if (tagNameLen < sizeof(tagName) - 1) tagName[tagNameLen++] = static_cast<char>(foldAscii(c));
          return;
        }
        tagNameDone = true;
      }
      tagSelfClosing = c == '/';
      return;

    case Mode::Entity:
      if (c == ';') {
        flushEntity(true);
        mode = Mode::Text;
        return;
      }
      if ((isWordByte(c) || c == '#') && entityLen < sizeof(entity) - 1) {
        entity[entityLen++] = static_cast<char>(c);
        return;
      }
      // Not an entity after all: emit what was collected, then reprocess c as text.
      flushEntity(false);
      mode = Mode::Text;
      --bytesSeen;
      feed(c);
      return;
  }
}

void BookTextScanner::flushEntity(const bool terminated) {
  entity[entityLen] = '\0';
  if (skipTag[0] != '\0') return;
  std::string decoded;
  if (terminated) {
    if (isTag(entity, "amp"))
      decoded = "&";
    else if (isTag(entity, "lt"))
      decoded = "<";
    else if (isTag(entity, "gt"))
      decoded = ">";
    else if (isTag(entity, "quot"))
      decoded = "\"";
    else if (isTag(entity, "apos"))
      decoded = "'";
    else if (isTag(entity, "nbsp"))
      decoded = " ";
    else if (isTag(entity, "shy"))
      decoded = "";  // soft hyphen: invisible, must not split a word
    else if (const char* named = namedEntity(entity))
      decoded = named;
    else if (entity[0] == '#') {
      const bool hex = entity[1] == 'x' || entity[1] == 'X';
      const uint32_t codepoint = static_cast<uint32_t>(std::strtoul(entity + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
      if (codepoint == 0xA0)
        decoded = " ";
      else if (codepoint > 0)
        appendUtf8(decoded, codepoint);
    } else {
      decoded = " ";  // unknown named entity: treat as a separator
    }
  } else {
    decoded = "&";
    decoded.append(entity, entityLen);
  }
  for (const char ch : decoded) emit(static_cast<uint8_t>(ch));
}

void BookTextScanner::handleTagEnd() {
  if (skipTag[0] != '\0') {
    if (tagClosing && isTag(tagName, skipTag)) skipTag[0] = '\0';
    return;
  }
  if (!tagClosing && !tagSelfClosing &&
      (isTag(tagName, "head") || isTag(tagName, "script") || isTag(tagName, "style"))) {
    std::strncpy(skipTag, tagName, sizeof(skipTag) - 1);
    skipTag[sizeof(skipTag) - 1] = '\0';
    return;
  }
  if (!isBlockTag(tagName)) return;
  // Count <p> the same way the chapter parser numbers paragraphs, so a hit
  // can be resolved to the page holding that paragraph.
  if (!tagClosing && isTag(tagName, "p") && paragraph < UINT16_MAX - 1) ++paragraph;
  emitBoundary();
}

void BookTextScanner::emitBoundary() {
  emit(' ');
  closePending();
  blockStart = emitted;
}

void BookTextScanner::emit(uint8_t c) {
  if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  if (c == ' ') {
    if (lastWasSpace) return;
    lastWasSpace = true;
  } else {
    lastWasSpace = false;
  }

  if (candidate) confirmCandidate(isWordByte(c));

  ring[emitted % RING] = c;
  ++emitted;
  if (pendingCount > 0) appendTrailing(c);

  const uint8_t folded = foldAscii(c);
  while (matched > 0 && static_cast<uint8_t>(query[matched]) != folded) matched = failure[matched - 1];
  if (static_cast<uint8_t>(query[matched]) == folded) ++matched;
  if (matched == query.size()) {
    const uint32_t start = emitted - static_cast<uint32_t>(query.size());
    const bool startOk = start == 0 || !isWordByte(ringAt(start - 1));
    if (startOk && !limitReached()) {
      candidate = true;
      candidateStart = start;
      candidateOffset = bytesSeen;
      candidateParagraph = paragraph;
    }
    matched = failure[matched - 1];
  }
}

void BookTextScanner::confirmCandidate(const bool nextIsWordChar) {
  candidate = false;
  if (nextIsWordChar || limitReached()) return;

  // Leading context: back to the block start or SNIPPET_BEFORE bytes, whichever is closer.
  uint32_t from = candidateStart > SNIPPET_BEFORE ? candidateStart - SNIPPET_BEFORE : 0;
  from = std::max(from, blockStart);
  if (emitted > RING - 1) from = std::max<uint32_t>(from, emitted - static_cast<uint32_t>(RING - 1));
  bool cutLeading = from > blockStart;
  if (cutLeading) {
    // Start at a whole word.
    uint32_t space = from;
    while (space < candidateStart && ringAt(space) != ' ') ++space;
    if (space < candidateStart) from = space + 1;
  }
  // Never start inside a multi-byte character.
  while (from < candidateStart && (ringAt(from) & 0xC0) == 0x80) ++from;

  Hit hit;
  hit.spineIndex = spine;
  hit.paragraphIndex = candidateParagraph == 0 ? UINT16_MAX : candidateParagraph;
  hit.byteOffset = candidateOffset;
  hit.snippet.reserve(SNIPPET_BEFORE + query.size() + SNIPPET_AFTER + 8);
  if (cutLeading) hit.snippet = ELLIPSIS;
  for (uint32_t i = from; i < emitted; ++i) hit.snippet += static_cast<char>(ringAt(i));
  hits.push_back(std::move(hit));

  if (pendingCount < MAX_PENDING) {
    pending[pendingCount++] = Pending{hits.size() - 1, SNIPPET_AFTER};
  } else {
    hits.back().snippet += ELLIPSIS;
  }
}

void BookTextScanner::appendTrailing(const uint8_t c) {
  size_t kept = 0;
  for (size_t i = 0; i < pendingCount; ++i) {
    Pending p = pending[i];
    std::string& snippet = hits[p.hitIndex].snippet;
    snippet += static_cast<char>(c);
    if (--p.remaining == 0) {
      // Cut at the last whole word, then mark the cut.
      const size_t space = snippet.find_last_of(' ');
      if (space != std::string::npos && space + 1 < snippet.size() && snippet.size() - space < SNIPPET_AFTER)
        snippet.resize(space);
      trimIncompleteUtf8Tail(snippet);
      snippet += ELLIPSIS;
      continue;
    }
    pending[kept++] = p;
  }
  pendingCount = kept;
}

void BookTextScanner::closePending() {
  for (size_t i = 0; i < pendingCount; ++i) {
    std::string& snippet = hits[pending[i].hitIndex].snippet;
    while (!snippet.empty() && snippet.back() == ' ') snippet.pop_back();
    trimIncompleteUtf8Tail(snippet);
  }
  pendingCount = 0;
}
