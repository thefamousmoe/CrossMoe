#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "Epub/blocks/ImageBlock.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"

// Full-screen viewer for one picture on the current EPUB page (maps, diagrams).
// Opened by the "Zoom Image" action. Confirm steps the zoom level, the
// direction buttons pan, Back returns to the book.
//
// Each zoom level is decoded once, in 2-bit gray, into a file on the SD card
// (see ImageBlock::buildZoomCache). Panning then only reads the visible rows
// back from that file, so moves are quick and look as sharp as the page, and
// no extra framebuffer-sized RAM is needed. If a cache cannot be built the
// viewer falls back to decoding each view directly in black and white.
class ImageZoomActivity final : public Activity {
 public:
  ImageZoomActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<ImageBlock> image);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  static constexpr int MAX_ZOOM = 4;

 private:
  // Size of the whole image at a zoom level, in screen pixels.
  void zoomedSize(int zoomLevel, int& width, int& height) const;
  void setZoom(int newZoom);
  void pan(int dx, int dy);
  void clampPan();
  void requestRedraw(bool fullRefresh);
  void close();
  bool canZoom() const;
  std::string cachePathFor(int width, int height) const;
  // Render task only: builds the current level's cache on first use.
  bool ensureZoomCache(int width, int height);
  void drawChrome(bool imageShown);

  std::unique_ptr<ImageBlock> image;
  int zoom = 1;
  // Highest useful level: past the picture's own resolution zooming only blurs.
  int maxZoom = MAX_ZOOM;
  // Top-left of the visible window inside the zoomed image.
  int panX = 0;
  int panY = 0;
  std::atomic<bool> needsRedraw{true};
  std::atomic<bool> needsFullRefresh{true};

  // Per zoom level (index = level). Written only by render(), read by onExit().
  std::string cachePaths[MAX_ZOOM + 1];
  bool cacheReady[MAX_ZOOM + 1] = {};
  bool cacheFailed[MAX_ZOOM + 1] = {};
};
