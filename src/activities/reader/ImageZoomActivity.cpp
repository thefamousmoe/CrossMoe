#include "ImageZoomActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Keep the zoomed image within sizes the decoders handle comfortably.
constexpr int MAX_ZOOMED_EDGE = 6144;
}  // namespace

ImageZoomActivity::ImageZoomActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::unique_ptr<ImageBlock> image)
    : Activity("ImageZoom", renderer, mappedInput), image(std::move(image)) {}

void ImageZoomActivity::onEnter() {
  Activity::onEnter();
  requestRedraw(true);
}

void ImageZoomActivity::onExit() { Activity::onExit(); }

bool ImageZoomActivity::canZoom() const {
  // The JPEG and PNG decoders clip negative offsets; other formats are shown
  // fitted to the screen only.
  const std::string& path = image->getImagePath();
  return FsHelpers::hasJpgExtension(path) || FsHelpers::hasPngExtension(path);
}

void ImageZoomActivity::zoomedSize(const int zoomLevel, int& width, int& height) const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int imageW = std::max<int>(1, image->getWidth());
  const int imageH = std::max<int>(1, image->getHeight());

  // Zoom 1 fits the whole picture on screen; each step multiplies that size.
  const float fit = std::min(static_cast<float>(screenW) / static_cast<float>(imageW),
                             static_cast<float>(screenH) / static_cast<float>(imageH));
  width = std::max(1, static_cast<int>(static_cast<float>(imageW) * fit * static_cast<float>(zoomLevel)));
  height = std::max(1, static_cast<int>(static_cast<float>(imageH) * fit * static_cast<float>(zoomLevel)));
}

void ImageZoomActivity::clampPan() {
  int w = 0;
  int h = 0;
  zoomedSize(zoom, w, h);
  const int maxX = std::max(0, w - renderer.getScreenWidth());
  const int maxY = std::max(0, h - renderer.getScreenHeight());
  panX = std::clamp(panX, 0, maxX);
  panY = std::clamp(panY, 0, maxY);
}

void ImageZoomActivity::setZoom(const int newZoom) {
  int oldW = 0;
  int oldH = 0;
  zoomedSize(zoom, oldW, oldH);
  int newW = 0;
  int newH = 0;
  zoomedSize(newZoom, newW, newH);
  if (newZoom > 1 && (newW > MAX_ZOOMED_EDGE || newH > MAX_ZOOMED_EDGE)) {
    // Too large to decode sensibly: wrap back to the fitted view instead.
    setZoom(1);
    return;
  }

  // Keep the point at the centre of the screen in the centre after zooming.
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const float centerX = (static_cast<float>(panX) + static_cast<float>(std::min(screenW, oldW)) / 2.0f) /
                        static_cast<float>(oldW);
  const float centerY = (static_cast<float>(panY) + static_cast<float>(std::min(screenH, oldH)) / 2.0f) /
                        static_cast<float>(oldH);
  zoom = newZoom;
  panX = static_cast<int>(centerX * static_cast<float>(newW)) - screenW / 2;
  panY = static_cast<int>(centerY * static_cast<float>(newH)) - screenH / 2;
  clampPan();
}

void ImageZoomActivity::pan(int dx, int dy) {
  int w = 0;
  int h = 0;
  zoomedSize(zoom, w, h);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  // With only one axis to move along, let every direction button move it.
  if (w <= screenW && dx != 0 && h > screenH) {
    dy = dx;
    dx = 0;
  } else if (h <= screenH && dy != 0 && w > screenW) {
    dx = dy;
    dy = 0;
  }
  const int oldX = panX;
  const int oldY = panY;
  panX += dx;
  panY += dy;
  clampPan();
  if (panX != oldX || panY != oldY) requestRedraw(false);
}

void ImageZoomActivity::requestRedraw(const bool fullRefresh) {
  if (fullRefresh) needsFullRefresh.store(true, std::memory_order_release);
  needsRedraw.store(true, std::memory_order_release);
  requestUpdate();
}

void ImageZoomActivity::close() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ImageZoomActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }

  const int stepX = renderer.getScreenWidth() * 2 / 3;
  const int stepY = renderer.getScreenHeight() * 2 / 3;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!canZoom()) return;
    {
      RenderLock lock(*this);
      setZoom(zoom >= MAX_ZOOM ? 1 : zoom + 1);
    }
    requestRedraw(true);
    return;
  }

  int dx = 0;
  int dy = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) dx = -stepX;
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) dx = stepX;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack))
    dy = -stepY;
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward))
    dy = stepY;
  if (dx != 0 || dy != 0) {
    RenderLock lock(*this);
    pan(dx, dy);
  }
}

void ImageZoomActivity::render(RenderLock&&) {
  if (!needsRedraw.exchange(false, std::memory_order_acq_rel)) return;
  const bool fullRefresh = needsFullRefresh.exchange(false, std::memory_order_acq_rel);

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int w = 0;
  int h = 0;
  zoomedSize(zoom, w, h);
  const int x = w <= screenW ? (screenW - w) / 2 : -panX;
  const int y = h <= screenH ? (screenH - h) / 2 : -panY;

  renderer.setRenderMode(GfxRenderer::BW);
  // Large zoomed decodes can take a few seconds; say so before the screen goes quiet.
  if (zoom > 1) GUI.drawPopup(renderer, tr(STR_LOADING), true);

  renderer.clearScreen();
  // One black-and-white dithered decode per view keeps panning responsive;
  // grayscale would need the image decoded four times.
  const bool ok = image->renderScaled(renderer, x, y, w, h, /*grayscale=*/false);
  if (!ok) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2, tr(STR_IMAGE_ZOOM_FAILED));
  }

  if (zoom == 1 || !ok) {
    // Hints only on the fitted view so they never cover part of a zoomed map.
    const char* zoomLabel = ok && canZoom() ? tr(STR_ZOOM_IN) : nullptr;
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), zoomLabel, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    char badge[8];
    snprintf(badge, sizeof(badge), "%dx", zoom);
    const int badgeW = renderer.getTextWidth(UI_10_FONT_ID, badge) + 12;
    const int badgeH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
    renderer.fillRect(screenW - badgeW - 4, 4, badgeW, badgeH, false);
    renderer.drawRect(screenW - badgeW - 4, 4, badgeW, badgeH, true);
    renderer.drawText(UI_10_FONT_ID, screenW - badgeW + 2, 7, badge, true);
  }

  renderer.cleanupGrayscaleWithFrameBuffer();
  renderer.displayBuffer(fullRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
