#pragma once
#include <HalStorage.h>
#include <Memory.h>

#include <memory>
#include <string>

#include "Block.h"

class ImageBlock final : public Block {
 public:
  ImageBlock(std::string imagePath, std::string sourcePath, int16_t width, int16_t height);
  ~ImageBlock() override = default;

  const std::string& getImagePath() const { return imagePath; }
  const std::string& getSourcePath() const { return sourcePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }

  void prepareCache(bool grayscale) const;
  bool hasValidCache(bool grayscale) const;
  bool needsDecode(bool grayscale) const;
  void renderPlaceholder(GfxRenderer& renderer, int x, int y, bool foregroundBlack) const;
  static void clearSessionRenderFailures();
  static void releaseSessionPixelCache();

  // The section builder only reads image headers. The reader supplies this
  // allocation-free callback pair to seed a bundled pixel cache or extract the
  // original image on its first render.
  using ExtractFn = bool (*)(void* context, const char* sourcePath, const char* destinationPath);
  using SeedCacheFn = bool (*)(void* context, const char* sourcePath, int width, int height,
                               const char* destinationPath);
  using LoadFn = HeapByteBuffer (*)(void*, const char*, size_t&);
  static void setExtractor(void* context, ExtractFn extract, SeedCacheFn seedCache, LoadFn load = nullptr);

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

  bool render(GfxRenderer& renderer, const int x, const int y, const bool foregroundBlack, bool imageGrayscale = true);
  // Decode the original image at an arbitrary size for the zoom viewer. x/y may
  // be negative: the decoders clip to the screen, so this draws one viewport of
  // a larger-than-screen image. Never reads or writes the page pixel cache.
  bool renderScaled(GfxRenderer& renderer, int x, int y, int drawWidth, int drawHeight, bool grayscale) const;
  // Zoom viewer cache: decode the whole image once at drawWidth x drawHeight
  // into a 2-bit gray file on the SD card, then draw any screen-sized window of
  // it without decoding again.
  bool buildZoomCache(GfxRenderer& renderer, const std::string& cachePath, int drawWidth, int drawHeight) const;
  static bool renderZoomCache(GfxRenderer& renderer, const std::string& cachePath, int x, int y, int drawWidth,
                              int drawHeight);
  // Pixel size of the original picture, for capping zoom at its real detail.
  bool sourceDimensions(int& width, int& height) const;
  bool serialize(Print& file);
  static std::unique_ptr<ImageBlock> deserialize(FsFile& file);

 private:
  std::string imagePath;
  std::string sourcePath;
  int16_t width;
  int16_t height;

  static void* extractContext;
  static ExtractFn extractFn;
  static SeedCacheFn seedCacheFn;
  static LoadFn loadFn;
};
