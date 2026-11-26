#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// Word rules shared by Search Book (which reads chapter HTML) and the reader
// (which reads laid-out pages), so both agree on what a word is. Pure string
// work with no I/O; small enough to inline.
namespace SearchText {

// Lower-cases ASCII, removes soft hyphens and surrounding punctuation, folds
// curly apostrophes to ' and turns dashes, curly double quotes and ellipses
// into spaces (they separate words). The result may contain spaces.
inline void normalizeWord(const char* word, std::string& out) {
  out.clear();
  for (const char* p = word; *p != '\0';) {
    const auto c = static_cast<unsigned char>(*p);
    if (c == 0xC2 && static_cast<unsigned char>(p[1]) == 0xAD) {  // soft hyphen
      p += 2;
      continue;
    }
    if (c == 0xE2 && static_cast<unsigned char>(p[1]) == 0x80 && p[2] != '\0') {
      const auto third = static_cast<unsigned char>(p[2]);
      if (third == 0x98 || third == 0x99) {  // curly single quote or apostrophe
        out.push_back('\'');
      } else if (third == 0x93 || third == 0x94 || third == 0x9C || third == 0x9D || third == 0xA6) {
        out.push_back(' ');  // dash, double quote, ellipsis: separate words
      } else {
        out.append(p, 3);
      }
      p += 3;
      continue;
    }
    out.push_back(c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
    ++p;
  }
  const auto isEdgePunct = [](const char ch) {
    const auto u = static_cast<unsigned char>(ch);
    return u < 0x80 && (std::ispunct(u) || std::isspace(u));
  };
  size_t start = 0;
  while (start < out.size() && isEdgePunct(out[start])) ++start;
  size_t end = out.size();
  while (end > start && isEdgePunct(out[end - 1])) --end;
  out = out.substr(start, end - start);
}

// Calls add(token) for each normalized word in `text` (split on spaces, then
// again on spaces normalization introduced, e.g. "Galen—who" -> galen, who).
template <typename Add>
void forEachToken(const std::string& text, Add&& add) {
  std::string normalized;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t space = text.find(' ', pos);
    const size_t end = space == std::string::npos ? text.size() : space;
    if (end > pos) {
      normalizeWord(text.substr(pos, end - pos).c_str(), normalized);
      size_t piece = 0;
      while (piece < normalized.size()) {
        const size_t gap = normalized.find(' ', piece);
        const size_t pieceEnd = gap == std::string::npos ? normalized.size() : gap;
        if (pieceEnd > piece) add(normalized.substr(piece, pieceEnd - piece));
        piece = pieceEnd + 1;
      }
    }
    pos = end + 1;
  }
}

inline void tokens(const std::string& text, std::vector<std::string>& out) {
  out.clear();
  forEachToken(text, [&](std::string token) { out.push_back(std::move(token)); });
}

// A page word matches a search token when equal, or when it extends the token
// with a possessive or compound ("galen's", "galen-like"), as the search does.
inline bool wordMatches(const std::string& pageWord, const std::string& token) {
  if (pageWord.size() < token.size() || pageWord.compare(0, token.size(), token) != 0) return false;
  return pageWord.size() == token.size() || pageWord[token.size()] == '\'' || pageWord[token.size()] == '-';
}

// 16-bit fingerprint of a normalized word; never 0, which means "no word".
inline uint16_t wordHash(const std::string& word) {
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const char c : word) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  const auto folded = static_cast<uint16_t>(hash ^ (hash >> 16));
  return folded == 0 ? 1 : folded;
}

// Fingerprints of the two words before and the two after a match, read from a
// result snippet: [before2, before1, after1, after2], 0 where there is none.
// `matchOffset` is the byte where the match starts in `snippet`;
// `queryTokenCount` is how many words the search text has.
inline void contextFromSnippet(const std::string& snippet, const size_t matchOffset, const size_t queryTokenCount,
                               uint16_t out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0;
  if (matchOffset > snippet.size()) return;
  std::vector<std::string> before;
  forEachToken(snippet.substr(0, matchOffset), [&](std::string t) { before.push_back(std::move(t)); });
  if (!before.empty()) out[1] = wordHash(before.back());
  if (before.size() >= 2) out[0] = wordHash(before[before.size() - 2]);
  size_t index = 0;
  forEachToken(snippet.substr(matchOffset), [&](const std::string& t) {
    if (index == queryTokenCount) out[2] = wordHash(t);
    if (index == queryTokenCount + 1) out[3] = wordHash(t);
    ++index;
  });
}

}  // namespace SearchText
