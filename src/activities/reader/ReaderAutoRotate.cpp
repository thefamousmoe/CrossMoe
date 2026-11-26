#include "ReaderAutoRotate.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"

namespace ReaderAutoRotate {

#ifdef SIMULATOR
// The simulator's stand-in sensor has no motion data.
void onReaderEnter() {}
int poll(uint8_t) { return -1; }
#else
namespace {

// One byte: 1 when sideways turns must go the other way on this device.
constexpr char MIRROR_FILE[] = "/.crosspoint/auto_rotate.bin";
// A by-hand 180 degree turn this soon after an automatic sideways turn means
// that guess was upside down.
constexpr unsigned long CORRECTION_WINDOW_MS = 20000;

struct State {
  bool mirrorLoaded = false;
  bool mirrored = false;
  int lastOrientation = -1;  // orientation seen on the previous poll
  int lastAutoOrientation = -1;
  bool lastAutoWasSideways = false;
  unsigned long lastAutoMs = 0;
};
State state;

void loadMirror() {
  if (state.mirrorLoaded) return;
  state.mirrorLoaded = true;
  HalFile file;
  if (!Storage.exists(MIRROR_FILE) || !Storage.openFileForRead("ROT", MIRROR_FILE, file)) return;
  uint8_t value = 0;
  if (file.read(&value, 1) == 1) state.mirrored = value == 1;
  file.close();
}

void saveMirror() {
  HalFile file;
  if (!Storage.openFileForWrite("ROT", MIRROR_FILE, file)) {
    LOG_ERR("ROT", "Could not save auto-rotate direction");
    return;
  }
  const uint8_t value = state.mirrored ? 1 : 0;
  file.write(&value, 1);
  file.close();
}

unsigned long flipHoldMs() {
  switch (SETTINGS.autoRotateDelay) {
    case CrossPointSettings::AUTO_ROTATE_DELAY_SHORT:
      return 600;
    case CrossPointSettings::AUTO_ROTATE_DELAY_LONG:
      return 1600;
    default:
      return 1000;
  }
}

unsigned long sidewaysHoldMs() {
  switch (SETTINGS.autoRotateDelay) {
    case CrossPointSettings::AUTO_ROTATE_DELAY_SHORT:
      return 1200;
    case CrossPointSettings::AUTO_ROTATE_DELAY_LONG:
      return 3000;
    default:
      return 2000;
  }
}

}  // namespace

void onReaderEnter() {
  halTiltSensor.resetAutoRotate();
  state.lastOrientation = -1;
  state.lastAutoOrientation = -1;
  state.lastAutoWasSideways = false;
}

int poll(const uint8_t currentOrientation) {
  const uint8_t mode = halTiltSensor.isAvailable() ? SETTINGS.autoRotate : CrossPointSettings::AUTO_ROTATE_OFF;
  halTiltSensor.setAutoRotate(mode, flipHoldMs(), sidewaysHoldMs());
  if (mode == CrossPointSettings::AUTO_ROTATE_OFF) {
    state.lastOrientation = currentOrientation;
    return -1;
  }

  // Someone other than auto-rotate changed the orientation (menu, button,
  // opening a book). The held position no longer matches the page, so wait for
  // the next page turn to pair them again.
  if (state.lastOrientation >= 0 && currentOrientation != state.lastOrientation) {
    const bool correctsLastGuess =
        state.lastAutoWasSideways && state.lastAutoOrientation >= 0 &&
        currentOrientation == ReaderUtils::flippedOrientation(static_cast<uint8_t>(state.lastAutoOrientation)) &&
        millis() - state.lastAutoMs < CORRECTION_WINDOW_MS;
    if (correctsLastGuess) {
      loadMirror();
      state.mirrored = !state.mirrored;
      saveMirror();
      LOG_INF("ROT", "Sideways direction corrected by hand; mirrored=%d", state.mirrored ? 1 : 0);
    }
    state.lastAutoWasSideways = false;
    halTiltSensor.resetAutoRotate(HalTiltSensor::AUTO_ANCHOR_AFTER_MANUAL_MS);
  }
  state.lastOrientation = currentOrientation;

  const uint8_t turns = halTiltSensor.takeAutoRotateTurns();
  if (turns == 0) return -1;

  loadMirror();
  const uint8_t target = orientationAfterTurns(currentOrientation, turns, state.mirrored);
  LOG_INF("ROT", "Auto-rotate %u -> %u (%u quarter turns)", currentOrientation, target, turns);
  state.lastAutoOrientation = target;
  state.lastAutoWasSideways = turns != 2;
  state.lastAutoMs = millis();
  state.lastOrientation = target;
  return target;
}
#endif  // SIMULATOR

}  // namespace ReaderAutoRotate
