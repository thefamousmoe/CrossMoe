#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "components/themes/BaseTheme.h"
#include "BookTextScanner.h"
#include "util/ButtonNavigator.h"

class Epub;

// Searches every chapter for one word, showing a progress bar, then lists the
// matches. Choosing one returns a BookmarkResult the reader jumps to.
class BookSearchActivity final : public Activity {
 public:
  BookSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                     std::string query);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
  bool preventAutoSleep() override { return state == State::Searching; }

 private:
  enum class State : uint8_t { Searching, Results, Failed };

  void searchNextChapter();
  void drawProgress(bool force);
  void finishSearch();
  void cancel();
  void selectResult();
  const char* chapterTitle(uint16_t spineIndex);

  static void provideRow(void* user, uint16_t index, freeink::ui::ListItem& item);
  static void listScreen(freeink::ui::FreeInkApp<20, 4>::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(freeink::ui::FreeInkApp<20, 4>::ScreenType& screen);

  std::shared_ptr<Epub> epub;
  std::string query;
  BookTextScanner scanner;
  std::atomic<State> state{State::Searching};
  int nextSpine = 0;
  int spineCount = 0;
  size_t bookSize = 0;
  Rect popupRect{};
  bool popupShown = false;
  int lastDrawnPercent = -1;
  uint32_t lastDrawMs = 0;

  struct ChapterName {
    uint16_t spineIndex;
    std::string title;
  };
  std::vector<ChapterName> chapterNames;
  char rowValue[8] = {};
  char headerSubtitle[40] = {};

  int selectedIndex = 0;
  int topIndex = 0;
  int visibleRows = 1;
  ButtonNavigator buttonNavigator;
  using UiApp = freeink::ui::FreeInkApp<20, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
};
