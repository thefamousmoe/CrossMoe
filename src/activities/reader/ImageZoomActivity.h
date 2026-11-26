#pragma once

#include <atomic>
#include <memory>

#include "Epub/blocks/ImageBlock.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"

// Full-screen viewer for one picture on the current EPUB page (maps, diagrams).
// Opened by the "Zoom Image" shortcut action. Confirm steps the zoom level,
// the direction buttons pan, Back returns to the book.
//
// Each view decodes the original image again at the zoomed size and lets the
// decoder clip it to the screen, so no extra framebuffer-sized memory is used.
class ImageZoomActivity final : public Activity {
 public:
  ImageZoomActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<ImageBlock> image);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  static constexpr int MAX_ZOOM = 4;

 private:
  // Size of the whole image at the current zoom level, in screen pixels.
  void zoomedSize(int zoomLevel, int& width, int& height) const;
  void setZoom(int newZoom);
  void pan(int dx, int dy);
  void clampPan();
  void requestRedraw(bool fullRefresh);
  void close();
  bool canZoom() const;

  std::unique_ptr<ImageBlock> image;
  int zoom = 1;
  // Top-left of the visible window inside the zoomed image.
  int panX = 0;
  int panY = 0;
  std::atomic<bool> needsRedraw{true};
  std::atomic<bool> needsFullRefresh{true};
};
