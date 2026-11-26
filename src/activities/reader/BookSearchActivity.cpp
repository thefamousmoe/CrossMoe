#include "BookSearchActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "FilenameFontSystem.h"
#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

// ---------------------------------------------------------------------------
// BookSearchActivity
// ---------------------------------------------------------------------------

BookSearchActivity::BookSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::shared_ptr<Epub> epub, std::string query)
    : Activity("BookSearch", renderer, mappedInput),
      epub(std::move(epub)),
      query(std::move(query)),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void BookSearchActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    filenameFontSystem.ensureLoaded(renderer);
  }
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &BookSearchActivity::onRowEvent, this);
  app.setScreen(&BookSearchActivity::listScreen, this);

  if (!epub || !scanner.setQuery(query)) {
    state = State::Failed;
    requestUpdate();
    return;
  }
  spineCount = epub->getSpineItemsCount();
  bookSize = epub->getBookSize();
  scanner.hits.reserve(32);
  LOG_INF("SRCH", "Searching %d chapters for \"%s\"", spineCount, query.c_str());
  drawProgress(true);
}

void BookSearchActivity::onExit() { Activity::onExit(); }

void BookSearchActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookSearchActivity::drawProgress(const bool force) {
  int percent = 0;
  if (nextSpine > 0 && bookSize > 0) {
    percent = static_cast<int>(100.0 * static_cast<double>(epub->getCumulativeSpineItemSize(nextSpine - 1)) /
                               static_cast<double>(bookSize));
  } else if (spineCount > 0) {
    percent = nextSpine * 100 / spineCount;
  }
  percent = std::clamp(percent, 0, 100);
  // Each panel update costs a refresh; only redraw for visible progress.
  if (!force && percent < lastDrawnPercent + 4 && millis() - lastDrawMs < 1500) return;

  RenderLock lock(*this);
  renderer.setRenderMode(GfxRenderer::BW);
  if (!popupShown) {
    popupRect = GUI.drawPopup(renderer, tr(STR_SEARCHING_BOOK));
    popupShown = true;
  }
  GUI.fillPopupProgress(renderer, popupRect, percent);
  lastDrawnPercent = percent;
  lastDrawMs = millis();
}

const char* BookSearchActivity::chapterTitle(const uint16_t spineIndex) {
  for (const auto& name : chapterNames) {
    if (name.spineIndex == spineIndex) return name.title.empty() ? nullptr : name.title.c_str();
  }
  return nullptr;
}

void BookSearchActivity::searchNextChapter() {
  if (nextSpine >= spineCount || scanner.limitReached()) {
    finishSearch();
    return;
  }

  const size_t hitsBefore = scanner.hits.size();
  const auto entry = epub->getSpineItem(nextSpine);
  scanner.beginChapter(static_cast<uint16_t>(nextSpine));
  if (!entry.href.empty() && !epub->readItemContentsToStream(entry.href, scanner, 1024, /*allowEarlyStop=*/true)) {
    LOG_ERR("SRCH", "Could not read chapter %d (%s)", nextSpine, entry.href.c_str());
  }
  scanner.endChapter();

  if (scanner.hits.size() > hitsBefore) {
    // Resolve the chapter name and book position once, here, rather than while drawing rows.
    const int tocIndex = epub->getTocIndexForSpineIndex(nextSpine);
    chapterNames.push_back(
        ChapterName{static_cast<uint16_t>(nextSpine), tocIndex >= 0 ? epub->getTocItem(tocIndex).title : ""});
    for (size_t i = hitsBefore; i < scanner.hits.size(); ++i) {
      auto& hit = scanner.hits[i];
      const float book = epub->calculateProgress(nextSpine, hit.chapterProgress);
      hit.bookPercent = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(book * 100.0f)), 0, 100));
    }
  }

  ++nextSpine;
  drawProgress(false);
}

void BookSearchActivity::finishSearch() {
  const size_t count = scanner.hits.size();
  snprintf(headerSubtitle, sizeof(headerSubtitle), scanner.limitReached() ? "%u+ %s" : "%u %s",
           static_cast<unsigned>(count), tr(STR_SEARCH_MATCHES));
  LOG_INF("SRCH", "Search for \"%s\" found %u matches", query.c_str(), static_cast<unsigned>(count));
  selectedIndex = 0;
  topIndex = 0;
  state = State::Results;
  requestUpdate();
}

void BookSearchActivity::selectResult() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(scanner.hits.size())) return;
  const auto& hit = scanner.hits[static_cast<size_t>(selectedIndex)];
  setResult(BookmarkResult{hit.spineIndex, hit.chapterProgress, hit.paragraphIndex});
  finish();
}

void BookSearchActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<BookSearchActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->scanner.hits.size())) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->selectResult();
}

void BookSearchActivity::loop() {
  if (state == State::Searching) {
    // Checked between chapters: hold Back for a moment to stop a long search.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Back)) {
      cancel();
      return;
    }
    searchNextChapter();
    return;
  }

  if (state == State::Failed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cancel();
    }
    return;
  }

  RenderLock lock(*this);
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput, safe);
  if (TouchHeaderBackButton::wasTapped(mappedInput, header) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lock.unlock();
    cancel();
    return;
  }
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectResult();
    return;
  }
  const int total = static_cast<int>(scanner.hits.size());
  if (total == 0) return;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int next = scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows,
                                  visibleRows, total);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }
  const auto move = [this, total](const int next) {
    selectedIndex = next;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, total);
    requestUpdate();
  };
  buttonNavigator.onNextRelease([this, total, &move] { move(ButtonNavigator::nextIndex(selectedIndex, total)); });
  buttonNavigator.onPreviousRelease(
      [this, total, &move] { move(ButtonNavigator::previousIndex(selectedIndex, total)); });
  buttonNavigator.onNextContinuous(
      [this, total, &move] { move(ButtonNavigator::nextPageIndex(selectedIndex, total, visibleRows)); });
  buttonNavigator.onPreviousContinuous(
      [this, total, &move] { move(ButtonNavigator::previousPageIndex(selectedIndex, total, visibleRows)); });
}

void BookSearchActivity::provideRow(void* user, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<BookSearchActivity*>(user);
  if (index >= self->scanner.hits.size()) return;
  const auto& hit = self->scanner.hits[index];
  snprintf(self->rowValue, sizeof(self->rowValue), "%u%%", static_cast<unsigned>(hit.bookPercent));
  item.label = hit.snippet.c_str();
  const char* title = self->chapterTitle(hit.spineIndex);
  item.subtitle = title ? title : tr(STR_UNKNOWN_CHAPTER);
  item.value = self->rowValue;
  item.actionValue = static_cast<int16_t>(index);
}

void BookSearchActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<BookSearchActivity*>(user)->buildListScreen(screen);
}

void BookSearchActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  setUiContentMargin(screen, renderer,
                     fui::Insets{static_cast<int16_t>(TouchHeaderBackButton::contentTop(renderer, mappedInput, safe.y)),
                                 static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                 static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                 static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (scanner.hits.empty()) {
    screen.centeredText(tr(STR_SEARCH_NO_MATCHES), screen.theme().bodyText);
    return;
  }
  FilenameUiFontScope fonts(uiTarget, renderer);
  fui::ListProps props;
  props.rowProvider = &BookSearchActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(scanner.hits.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.valueInset = 8;
  const fui::Rect bounds = screen.body();
  const auto rows = configureUiList(props, screen.theme(), bounds, UiListRowType::WithSubtitle);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(scanner.hits.size()));
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void BookSearchActivity::render(RenderLock&&) {
  // While searching, loop() owns the screen and draws the progress popup itself.
  if (state == State::Searching) return;

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput, safe);

  if (state == State::Failed) {
    GUI.drawHeader(renderer, header, tr(STR_SEARCH_BOOK), nullptr, true);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_SEARCH_FAILED));
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  char title[96];
  snprintf(title, sizeof(title), "\xE2\x80\x9C%s\xE2\x80\x9D", query.c_str());
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, title, true);
  } else {
    GUI.drawHeader(renderer, header, title, headerSubtitle, true);
  }
  uiReady = false;
  renderUiApp(app, uiTarget);
  uiReady = true;
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), scanner.hits.empty() ? "" : tr(STR_SELECT),
                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  // The first list frame replaces the progress popup; a half refresh clears its ghost.
  renderer.displayBuffer(popupShown ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  popupShown = false;
}
