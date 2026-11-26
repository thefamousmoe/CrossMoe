#pragma once

#include <cstdint>

// Reader side of auto-rotate: turns the motion sensor's "device was turned N
// quarter turns" reports into a reader orientation, and learns from the user
// when a sideways guess was upside down. Shared by the EPUB and TXT readers;
// only one reader is open at a time, so the state is a single static.
namespace ReaderAutoRotate {

// Call when a reader opens: the position the device is held in is adopted
// once it is held still briefly (or at the first button press).
void onReaderEnter();

// Call once per reader loop() while the page is showing. Returns the
// orientation to switch to, or -1 to stay. `currentOrientation` is
// SETTINGS.orientation.
int poll(uint8_t currentOrientation);

// Orientation to show after the device turned `quarterTurns` counter-clockwise
// quarter turns in the sensor's frame. `mirrored` flips the sideways direction.
//
// From the X3 tilt calibration (HalTiltSensor): the screen's "up" lies along
// +X in portrait, +Y in landscape CCW, -X inverted and -Y landscape CW, up to
// one shared sign. So a counter-clockwise quarter turn in the sensor's frame
// steps the orientation order (0 portrait, 1 landscape CW, 2 inverted,
// 3 landscape CCW) back by one. Flips (two turns) are the same either way.
inline uint8_t orientationAfterTurns(const uint8_t currentOrientation, const uint8_t quarterTurns,
                                     const bool mirrored) {
  const uint8_t steps = mirrored ? quarterTurns % 4 : (4 - quarterTurns % 4) % 4;
  return static_cast<uint8_t>((currentOrientation + steps) % 4);
}

// Fixed link between the sensor and the screen, learned while reading: the
// orientation that reads upright when the device rests in `zone` (0-3, see
// AutoRotateDetector::zoneFor). Stored as one number so a book opened later can
// start the right way up for how the device is held. Consistent with
// orientationAfterTurns(): turning the device moves both by the same steps.
inline uint8_t uprightOffset(const uint8_t orientation, const uint8_t zone, const bool mirrored) {
  return mirrored ? static_cast<uint8_t>((orientation + 4 - zone % 4) % 4)
                  : static_cast<uint8_t>((orientation + zone) % 4);
}

inline uint8_t orientationForZone(const uint8_t offset, const uint8_t zone, const bool mirrored) {
  return mirrored ? static_cast<uint8_t>((offset + zone) % 4) : static_cast<uint8_t>((offset + 4 - zone % 4) % 4);
}

}  // namespace ReaderAutoRotate
