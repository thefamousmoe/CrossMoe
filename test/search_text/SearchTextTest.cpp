#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "SearchText.h"

TEST(SearchText, NormalizesPunctuationCaseAndQuotes) {
  std::string out;
  SearchText::normalizeWord("\xE2\x80\x9CGalen,", out);
  EXPECT_EQ(out, "galen");
  SearchText::normalizeWord("Galen\xE2\x80\x99s", out);
  EXPECT_EQ(out, "galen's");
  SearchText::normalizeWord("Kin\xC2\xADgan", out);
  EXPECT_EQ(out, "kingan");
}

TEST(SearchText, DashesSplitWords) {
  std::vector<std::string> tokens;
  SearchText::tokens("Galen\xE2\x80\x94who knew", tokens);
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0], "galen");
  EXPECT_EQ(tokens[1], "who");
}

TEST(SearchText, MatchesPossessivesButNotLongerWords) {
  EXPECT_TRUE(SearchText::wordMatches("galen's", "galen"));
  EXPECT_TRUE(SearchText::wordMatches("galen-like", "galen"));
  EXPECT_FALSE(SearchText::wordMatches("galena", "galen"));
}

TEST(SearchText, ContextComesFromTheWordsAroundTheMatch) {
  const std::string snippet =
      "\xE2\x80\xA6"
      "and then Galen turned toward the door\xE2\x80\xA6";
  const size_t matchOffset = snippet.find("Galen");
  uint16_t context[4];
  SearchText::contextFromSnippet(snippet, matchOffset, 1, context);
  EXPECT_EQ(context[0], SearchText::wordHash("and"));
  EXPECT_EQ(context[1], SearchText::wordHash("then"));
  EXPECT_EQ(context[2], SearchText::wordHash("turned"));
  EXPECT_EQ(context[3], SearchText::wordHash("toward"));
}

TEST(SearchText, MissingNeighborsAreZero) {
  const std::string snippet = "Galen.";
  uint16_t context[4];
  SearchText::contextFromSnippet(snippet, 0, 1, context);
  EXPECT_EQ(context[0], 0);
  EXPECT_EQ(context[1], 0);
  EXPECT_EQ(context[2], 0);
  EXPECT_EQ(context[3], 0);
}
