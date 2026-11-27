#include "ReaderAutoRotate.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"

namespace ReaderAutoRotate {

bool toggleSetting() {
  if (SETTINGS.autoRotate != CrossPointSettings::AUTO_ROTATE_OFF) {
    SETTINGS.autoRotateLastOn = SETTINGS.autoRotate;
    SETTINGS.autoRotate = CrossPointSettings::AUTO_ROTATE_OFF;
    return false;
  }
  const uint8_t mode = SETTINGS.autoRotateLastOn;
  SETTINGS.autoRotate = mode == CrossPointSettings::AUTO_ROTATE_OFF || mode > CrossPointSettings::AUTO_ROTATE_SIDEWAYS
                            ? CrossPointSettings::AUTO_ROTATE_ALL
                            : mode;
  // Start fresh, as when a book opens: the next resting position is checked
  // against what was learned and the page turned to match if needed.
  onReaderEnter();
  return true;
}

#ifdef SIMULATOR
// The simulator's stand-in sensor has no motion data.
void onReaderEnter() {}
int poll(uint8_t) { return -1; }
#else
namespace {

// Byte 0: 1 when sideways turns must go the other way on this device.
// Byte 1: learned upright offset (see uprightOffset()), or 0xFF before one is
// learned. Files from older builds have only byte 0.
constexpr char MIRROR_FILE[] = "/.crosspoint/auto_rotate.bin";
constexpr uint8_t NO_OFFSET = 0xFF;
// A by-hand 180 degree turn this soon after an automatic sideways turn means
// that guess was upside down.
constexpr unsigned long CORRECTION_WINDOW_MS = 20000;

struct State {
  bool mirrorLoaded = false;
  bool mirrored = false;
  uint8_t offset = NO_OFFSET;  // learned link between sensor and screen
  // Set when a book opens: the first resting position adopted is checked
  // against the learned offset instead of being assumed upright.
  bool checkOpenPosition = false;
  // Whether the page on screen got its orientation from the sensor (or the
  // open check), so a button press can teach the upright position. False after
  // an orientation chosen by hand.
  bool trustPairing = false;
  unsigned long lastOpenTurnMs = 0;  // when the open check last turned the page, or 0
  int lastOrientation = -1;          // orientation seen on the previous poll
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
  uint8_t bytes[2] = {0, NO_OFFSET};
  const int read = file.read(bytes, 2);
  if (read >= 1) state.mirrored = bytes[0] == 1;
  if (read >= 2 && bytes[1] < 4) state.offset = bytes[1];
  file.close();
}

void saveMirror() {
  HalFile file;
  if (!Storage.openFileForWrite("ROT", MIRROR_FILE, file)) {
    LOG_ERR("ROT", "Could not save auto-rotate direction");
    return;
  }
  const uint8_t bytes[2] = {static_cast<uint8_t>(state.mirrored ? 1 : 0), state.offset};
  file.write(bytes, 2);
  file.close();
}

unsigned long flipHoldMs() {
  switch (SETTINGS.autoRotateDelay) {
    case CrossPointSettings::AUTO_ROTATE_DELAY_SHORT:
      return 250;
    case CrossPointSettings::AUTO_ROTATE_DELAY_LONG:
      return 1600;
    default:
      return 1000;
  }
}

unsigned long sidewaysHoldMs() {
  switch (SETTINGS.autoRotateDelay) {
    case CrossPointSettings::AUTO_ROTATE_DELAY_SHORT:
      return 250;
    case CrossPointSettings::AUTO_ROTATE_DELAY_LONG:
      return 3000;
    default:
      return 2000;
  }
}

// Sideways never shows portrait upside down; that position shows normal
// portrait instead.
uint8_t allowedOrientation(const uint8_t mode, const uint8_t orientation) {
  if (mode == CrossPointSettings::AUTO_ROTATE_SIDEWAYS && orientation == CrossPointSettings::INVERTED) {
    return CrossPointSettings::PORTRAIT;
  }
  return orientation;
}

bool allowsSideways(const uint8_t mode) {
  return mode == CrossPointSettings::AUTO_ROTATE_ALL || mode == CrossPointSettings::AUTO_ROTATE_SIDEWAYS;
}

}  // namespace

void onReaderEnter() {
  halTiltSensor.resetAutoRotate();
  state.checkOpenPosition = true;
  state.trustPairing = false;
  state.lastOrientation = -1;
  state.lastAutoOrientation = -1;
  state.lastAutoWasSideways = false;
  state.lastOpenTurnMs = 0;
}

int poll(const uint8_t currentOrientation) {
  const uint8_t mode = halTiltSensor.isAvailable() ? SETTINGS.autoRotate : CrossPointSettings::AUTO_ROTATE_OFF;
  halTiltSensor.setAutoRotate(mode, flipHoldMs(), sidewaysHoldMs());
  const uint8_t readingZone = halTiltSensor.takeReadingPosition();
  if (mode == CrossPointSettings::AUTO_ROTATE_OFF) {
    state.lastOrientation = currentOrientation;
    return -1;
  }
  loadMirror();

  // Someone other than auto-rotate changed the orientation (menu, button). The
  // held position no longer matches the page, so pair them again later.
  if (state.lastOrientation >= 0 && currentOrientation != state.lastOrientation) {
    const unsigned long now = millis();
    const bool correctsLastGuess =
        state.lastAutoWasSideways && state.lastAutoOrientation >= 0 &&
        currentOrientation == ReaderUtils::flippedOrientation(static_cast<uint8_t>(state.lastAutoOrientation)) &&
        now - state.lastAutoMs < CORRECTION_WINDOW_MS;
    const bool undoesOpenTurn = state.lastOpenTurnMs != 0 && now - state.lastOpenTurnMs < CORRECTION_WINDOW_MS;
    if (correctsLastGuess) {
      state.mirrored = !state.mirrored;
      state.offset = NO_OFFSET;  // learned with the old direction; learn it again
      saveMirror();
      LOG_INF("ROT", "Sideways direction corrected by hand; mirrored=%d", state.mirrored ? 1 : 0);
    } else if (undoesOpenTurn && state.offset != NO_OFFSET) {
      // The page was turned on opening, then changed by hand: what was learned
      // does not fit how this person reads now. Learn it again.
      state.offset = NO_OFFSET;
      saveMirror();
      LOG_INF("ROT", "Opening turn changed by hand; forgetting the learned upright position");
    }
    state.lastAutoWasSideways = false;
    state.lastOpenTurnMs = 0;
    state.checkOpenPosition = false;
    // A by-hand choice may not follow gravity (reading lying down), so button
    // presses after it do not teach the upright position.
    state.trustPairing = false;
    halTiltSensor.resetAutoRotate(HalTiltSensor::AUTO_ANCHOR_AFTER_MANUAL_MS);
  }
  state.lastOrientation = currentOrientation;

  if (state.checkOpenPosition) {
    // Book just opened. The first position adopted (held still, or at a button
    // press such as the one that woke the reader) may not match the page left
    // from last time: closed in landscape, picked up in portrait. Turn the page
    // to what the learned offset says reads upright there. Button presses
    // before that say nothing about which way is up.
    const uint8_t openZone = halTiltSensor.autoRotateAnchorPosition();
    if (openZone != AutoRotateDetector::NO_ZONE) {
      state.checkOpenPosition = false;
      if (state.offset == NO_OFFSET) {
        state.trustPairing = true;  // nothing learned yet: assume upright, as before
      } else {
        const uint8_t target = allowedOrientation(mode, orientationForZone(state.offset, openZone, state.mirrored));
        const uint8_t steps = static_cast<uint8_t>((target + 4 - currentOrientation) % 4);
        const bool allowed = steps == 2 || allowsSideways(mode);
        state.trustPairing = target == currentOrientation || allowed;
        if (target != currentOrientation && allowed) {
          LOG_INF("ROT", "Opened held in zone %u: %u -> %u", openZone, currentOrientation, target);
          state.lastOpenTurnMs = millis() | 1;  // never 0, which means "none"
          state.lastOrientation = target;
          return target;
        }
      }
    }
  } else if (readingZone != AutoRotateDetector::NO_ZONE && (state.trustPairing || state.offset == NO_OFFSET)) {
    // A button press while reading: the page is upright for how the device is
    // held. Remember that link for the next book opened.
    const uint8_t offset = uprightOffset(currentOrientation, readingZone, state.mirrored);
    if (offset != state.offset) {
      state.offset = offset;
      saveMirror();
      LOG_INF("ROT", "Learned upright position: zone %u shows orientation %u", readingZone, currentOrientation);
    }
    state.trustPairing = true;
  }

  const uint8_t turns = halTiltSensor.takeAutoRotateTurns();
  if (turns == 0) return -1;

  const uint8_t target = allowedOrientation(mode, orientationAfterTurns(currentOrientation, turns, state.mirrored));
  if (target == currentOrientation) {
    LOG_INF("ROT", "Auto-rotate %u turn(s) kept: Sideways never shows portrait upside down", turns);
    return -1;
  }
  LOG_INF("ROT", "Auto-rotate %u -> %u (%u quarter turns)", currentOrientation, target, turns);
  state.lastAutoOrientation = target;
  state.lastAutoWasSideways = turns != 2;
  state.lastAutoMs = millis();
  state.lastOrientation = target;
  return target;
}
#endif  // SIMULATOR

}  // namespace ReaderAutoRotate
