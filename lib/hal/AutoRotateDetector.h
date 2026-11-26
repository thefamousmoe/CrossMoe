#pragma once

#include <cmath>
#include <cstdint>

// Decides when the device has been turned to a new resting position, from the
// accelerometer's gravity reading. Pure logic with no I/O, so it can be unit
// tested on the host.
//
// It does not need to know how the sensor chip is mounted. It remembers an
// "anchor": the position the device was held in while the page on screen was
// upright (set by a button page turn, or after an auto-rotation). update()
// then reports how many quarter turns, counted in the sensor's own X/Y plane,
// the device has been turned away from that anchor once it has rested there
// long enough. Mapping those turns to reader orientations is the caller's job.
class AutoRotateDetector {
 public:
  struct Sample {
    float ax, ay, az;     // acceleration, g
    float gyroDps;        // largest absolute angular rate, deg/s (0 if no gyro)
    unsigned long nowMs;  // millis() of the reading
  };

  // Forgets the anchor and any pending turn. Call when the screen orientation
  // changes for another reason, or when the sensor restarts. With
  // autoAnchorMs > 0 the first position held still that long becomes the new
  // anchor by itself (a book just opened is being read upright); 0 waits for
  // anchorToCurrentPosition().
  void reset(const unsigned long autoAnchorMs = 0) {
    anchorZone_ = NO_ZONE;
    lastZone_ = NO_ZONE;
    autoAnchorMs_ = autoAnchorMs;
    clearAnchorCandidate();
    clearCandidate();
  }

  bool hasAnchor() const { return anchorZone_ != NO_ZONE; }
  uint8_t anchorZone() const { return anchorZone_; }
  // Resting position from the latest reading, or NO_ZONE (flat, moving, shaken).
  uint8_t currentZone() const { return lastZone_; }

  // Records the current resting position as matching the upright page. Only
  // sets a missing anchor: a page turn made while the device is part-way
  // through being turned must not cancel that turn.
  void anchorToCurrentPosition() {
    if (anchorZone_ == NO_ZONE && lastZone_ != NO_ZONE) {
      anchorZone_ = lastZone_;
      clearAnchorCandidate();
    }
  }

  // Restarts the hold timer, e.g. after a tilt page-turn flick.
  void interrupt() { clearCandidate(); }

  // Feeds one reading. Returns 0 normally, or 1-3 quarter turns (counter-
  // clockwise about the sensor's +Z axis) once the device has rested in a new
  // position for the hold time. The anchor then moves to that position.
  uint8_t update(const Sample& s, const bool allowSideways, const unsigned long flipHoldMs,
                 const unsigned long sidewaysHoldMs) {
    lastZone_ = zoneFor(s);
    const uint8_t zone = lastZone_;
    if (anchorZone_ == NO_ZONE) {
      clearCandidate();
      updateAutoAnchor(zone, s.nowMs);
      return 0;
    }
    if (zone == NO_ZONE || zone == anchorZone_) {
      clearCandidate();
      return 0;
    }

    const uint8_t turns = static_cast<uint8_t>((zone + 4 - anchorZone_) % 4);
    if (turns != 2 && !allowSideways) {
      clearCandidate();
      return 0;
    }
    if (candidateZone_ != zone) {
      candidateZone_ = zone;
      candidateSinceMs_ = s.nowMs;
      return 0;
    }
    const unsigned long holdMs = turns == 2 ? flipHoldMs : sidewaysHoldMs;
    if (s.nowMs - candidateSinceMs_ < holdMs) return 0;

    anchorZone_ = zone;
    clearCandidate();
    return turns;
  }

  // Tuning, public so tests can reason about the boundaries.
  static constexpr float MIN_G = 0.75f;  // reject readings while the device is being shaken or carried
  static constexpr float MAX_G = 1.25f;
  // Below this share of gravity in the screen plane the device is treated as
  // lying flat (under ~30 degrees from horizontal), like phones do.
  static constexpr float MIN_UPRIGHT_FRACTION = 0.5f;
  static constexpr float STILL_DPS = 40.0f;  // faster than this counts as still turning
  // Each resting position covers +/-30 degrees around its axis; the 30 degree
  // gaps between them stop a device held near 45 degrees from flickering.
  static constexpr float ZONE_HALF_WIDTH_DEG = 30.0f;
  static constexpr uint8_t NO_ZONE = 0xFF;

  // Resting position for a reading: 0 = +X up, 1 = +Y, 2 = -X, 3 = -Y, or NO_ZONE.
  static uint8_t zoneFor(const Sample& s) {
    if (s.gyroDps > STILL_DPS) return NO_ZONE;
    const float total = std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    if (total < MIN_G || total > MAX_G) return NO_ZONE;
    const float inPlane = std::sqrt(s.ax * s.ax + s.ay * s.ay);
    if (inPlane < MIN_UPRIGHT_FRACTION * total) return NO_ZONE;

    constexpr float kDegreesPerRadian = 57.2957795f;
    float angle = std::atan2(s.ay, s.ax) * kDegreesPerRadian;  // -180..180, 0 = +X
    if (angle < 0) angle += 360.0f;
    for (uint8_t zone = 0; zone < 4; ++zone) {
      float diff = angle - 90.0f * zone;
      if (diff > 180.0f) diff -= 360.0f;
      if (diff < -180.0f) diff += 360.0f;
      if (std::fabs(diff) <= ZONE_HALF_WIDTH_DEG) return zone;
    }
    return NO_ZONE;
  }

 private:
  void clearCandidate() {
    candidateZone_ = NO_ZONE;
    candidateSinceMs_ = 0;
  }

  void clearAnchorCandidate() {
    anchorCandidateZone_ = NO_ZONE;
    anchorCandidateSinceMs_ = 0;
  }

  // Adopts a position held still for autoAnchorMs_ as the anchor.
  void updateAutoAnchor(const uint8_t zone, const unsigned long nowMs) {
    if (autoAnchorMs_ == 0 || zone == NO_ZONE) {
      clearAnchorCandidate();
      return;
    }
    if (anchorCandidateZone_ != zone) {
      anchorCandidateZone_ = zone;
      anchorCandidateSinceMs_ = nowMs;
      return;
    }
    if (nowMs - anchorCandidateSinceMs_ >= autoAnchorMs_) {
      anchorZone_ = zone;
      clearAnchorCandidate();
    }
  }

  uint8_t anchorZone_ = NO_ZONE;
  uint8_t lastZone_ = NO_ZONE;
  uint8_t candidateZone_ = NO_ZONE;
  unsigned long candidateSinceMs_ = 0;
  unsigned long autoAnchorMs_ = 0;
  uint8_t anchorCandidateZone_ = NO_ZONE;
  unsigned long anchorCandidateSinceMs_ = 0;
};
