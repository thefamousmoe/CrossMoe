#include "ImageZoomActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "activities/util/ImageViewerGrayscale.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Keep the zoomed image within sizes the decoders and the 16-bit cache header handle.
constexpr int MAX_ZOOMED_EDGE = 6144;
}  // namespace

ImageZoomActivity::ImageZoomActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::unique_ptr<ImageBlock> image)
    : Activity("ImageZoom", renderer, mappedInput), image(std::move(image)) {}

void ImageZoomActivity::onEnter() {
  Activity::onEnter();

  // Logged to help diagnose pictures that do not zoom; zooming itself always
  // offers every level (small pictures simply look softer at 3x-4x).
  int sourceWidth = 0;
  int sourceHeight = 0;
  if (image->sourceDimensions(sourceWidth, sourceHeight)) {
    int fitWidth = 0;
    int fitHeight = 0;
    zoomedSize(1, fitWidth, fitHeight);
    LOG_INF("ZOOM", "Source %dx%d, fitted %dx%d, path %s", sourceWidth, sourceHeight, fitWidth, fitHeight,
            image->getImagePath().c_str());
  }
  requestRedraw(true);
}

void ImageZoomActivity::onExit() {
  // The cached levels are only useful while this picture is open; free the SD space.
  for (int level = 1; level <= MAX_ZOOM; ++level) {
    if (cacheReady[level] && !cachePaths[level].empty()) Storage.remove(cachePaths[level].c_str());
  }
  Activity::onExit();
}

bool ImageZoomActivity::canZoom() const {
  // The JPEG and PNG decoders can cache and clip; other formats are shown fitted only.
  const std::string& path = image->getImagePath();
  return FsHelpers::hasJpgExtension(path) || FsHelpers::hasPngExtension(path);
}

std::string ImageZoomActivity::cachePathFor(const int width, const int height) const {
  const std::string& path = image->getImagePath();
  const size_t dot = path.rfind('.');
  char suffix[32];
  snprintf(suffix, sizeof(suffix), "_zoom_%dx%d.pxc", width, height);
  return (dot != std::string::npos ? path.substr(0, dot) : path) + suffix;
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
  const float centerX =
      (static_cast<float>(panX) + static_cast<float>(std::min(screenW, oldW)) / 2.0f) / static_cast<float>(oldW);
  const float centerY =
      (static_cast<float>(panY) + static_cast<float>(std::min(screenH, oldH)) / 2.0f) / static_cast<float>(oldH);
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

bool ImageZoomActivity::ensureZoomCache(const int width, const int height) {
  if (zoom < 1 || zoom > MAX_ZOOM) return false;
  const std::string path = cachePathFor(width, height);
  if (cacheReady[zoom] && cachePaths[zoom] == path) return true;
  if (cacheFailed[zoom] && cachePaths[zoom] == path) return false;

  cachePaths[zoom] = path;
  // Decoding the whole level takes a few seconds once; later moves reuse it.
  // Drawn into the framebuffer for real (no backdrop restore) so the next
  // refresh compares against what the panel actually shows.
  GUI.drawPopup(renderer, tr(STR_LOADING));
  cacheReady[zoom] = image->buildZoomCache(renderer, path, width, height);
  cacheFailed[zoom] = !cacheReady[zoom];
  return cacheReady[zoom];
}

void ImageZoomActivity::drawChrome(const bool imageShown) {
  const int screenW = renderer.getScreenWidth();
  if (zoom == 1 || !imageShown) {
    // Hints only on the fitted view so they never cover part of a zoomed map.
    const char* zoomLabel = imageShown && canZoom() ? tr(STR_ZOOM_IN) : nullptr;
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), zoomLabel, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  char badge[8];
  snprintf(badge, sizeof(badge), "%dx", zoom);
  const int badgeW = renderer.getTextWidth(UI_10_FONT_ID, badge) + 12;
  const int badgeH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  renderer.fillRect(screenW - badgeW - 4, 4, badgeW, badgeH, false);
  renderer.drawRect(screenW - badgeW - 4, 4, badgeW, badgeH, true);
  renderer.drawText(UI_10_FONT_ID, screenW - badgeW + 2, 7, badge, true);
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

  // Preferred path: gray, drawn from this level's SD cache (JPEG and PNG only).
  const std::string& imagePath = image->getImagePath();
  if (FsHelpers::hasJpgExtension(imagePath) || FsHelpers::hasPngExtension(imagePath)) {
    if (ensureZoomCache(w, h)) {
      const std::string& path = cachePaths[zoom];
      const bool shown = renderImageViewerGrayscale(renderer, [&](bool /*grayscale*/, const bool completeFrame) {
        if (!ImageBlock::renderZoomCache(renderer, path, x, y, w, h)) return false;
        if (completeFrame) drawChrome(true);
        return true;
      });
      renderer.setRenderMode(GfxRenderer::BW);
      if (shown) return;
      LOG_ERR("ZOOM", "Cached view failed; decoding directly");
    }
  }

  // Fallback: decode this view directly, in black and white.
  renderer.clearScreen();
  const bool ok = image->renderScaled(renderer, x, y, w, h, /*grayscale=*/false);
  if (!ok) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2, tr(STR_IMAGE_ZOOM_FAILED));
  }
  drawChrome(ok);
  renderer.cleanupGrayscaleWithFrameBuffer();
  renderer.displayBuffer(fullRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
