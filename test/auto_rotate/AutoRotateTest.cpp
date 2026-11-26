#include <gtest/gtest.h>

#include <cmath>

#include "AutoRotateDetector.h"
#include "ReaderAutoRotate.h"

namespace {

constexpr float PI_F = 3.14159265f;

// A reading with the device resting upright at `degrees` in the sensor's X/Y
// plane (0 = +X up), tilted back from vertical by `tiltBackDeg`.
AutoRotateDetector::Sample resting(const float degrees, const unsigned long nowMs, const float tiltBackDeg = 20.0f,
                                   const float gyroDps = 2.0f) {
  const float rad = degrees * PI_F / 180.0f;
  const float tilt = tiltBackDeg * PI_F / 180.0f;
  const float inPlane = std::cos(tilt);
  return {inPlane * std::cos(rad), inPlane * std::sin(rad), std::sin(tilt), gyroDps, nowMs};
}

// Feeds 20 Hz readings at one position; returns the first non-zero result.
uint8_t hold(AutoRotateDetector& d, const float degrees, unsigned long& now, const unsigned long durationMs,
             const bool allowSideways = true, const float tiltBackDeg = 20.0f) {
  uint8_t result = 0;
  for (unsigned long end = now + durationMs; now <= end; now += 50) {
    const uint8_t turns = d.update(resting(degrees, now, tiltBackDeg), allowSideways, 1000, 2000);
    if (turns != 0 && result == 0) result = turns;
  }
  return result;
}

constexpr uint8_t PORTRAIT = 0;
constexpr uint8_t LANDSCAPE_CW = 1;
constexpr uint8_t INVERTED = 2;
constexpr uint8_t LANDSCAPE_CCW = 3;

}  // namespace

TEST(AutoRotateDetector, ZonesFollowGravityInTheScreenPlane) {
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(0, 0)), 0);
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(90, 0)), 1);
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(180, 0)), 2);
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(-90, 0)), 3);
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(25, 0)), 0);
  // Near 45 degrees is a dead band, so an in-between hold never flickers.
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(45, 0)), AutoRotateDetector::NO_ZONE);
}

TEST(AutoRotateDetector, IgnoresFlatMovingAndShakenReadings) {
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(0, 0, /*tiltBack=*/75)), AutoRotateDetector::NO_ZONE);
  EXPECT_EQ(AutoRotateDetector::zoneFor(resting(0, 0, 20, /*gyro=*/120)), AutoRotateDetector::NO_ZONE);
  AutoRotateDetector::Sample shaken = resting(0, 0);
  shaken.ax *= 1.6f;
  EXPECT_EQ(AutoRotateDetector::zoneFor(shaken), AutoRotateDetector::NO_ZONE);
}

TEST(AutoRotateDetector, DoesNothingUntilAPageTurnAnchorsThePosition) {
  AutoRotateDetector d;
  unsigned long now = 0;
  EXPECT_EQ(hold(d, 0, now, 3000), 0);
  EXPECT_EQ(hold(d, 180, now, 3000), 0);  // no anchor: turning is ignored
  EXPECT_FALSE(d.hasAnchor());
  d.anchorToCurrentPosition();
  EXPECT_TRUE(d.hasAnchor());
  EXPECT_EQ(hold(d, 180, now, 500), 0);  // already there
}

TEST(AutoRotateDetector, FlipFiresAfterTheFlipHold) {
  AutoRotateDetector d;
  unsigned long now = 0;
  hold(d, 0, now, 200);
  d.anchorToCurrentPosition();
  EXPECT_EQ(hold(d, 180, now, 900), 0);   // not yet
  EXPECT_EQ(hold(d, 180, now, 200), 2);   // ~1 s in the new position
  EXPECT_EQ(hold(d, 180, now, 3000), 0);  // the anchor moved; it fires once
}

TEST(AutoRotateDetector, SidewaysNeedsTheLongerHoldAndTheSetting) {
  AutoRotateDetector d;
  unsigned long now = 0;
  hold(d, 0, now, 200);
  d.anchorToCurrentPosition();
  EXPECT_EQ(hold(d, 90, now, 3000, /*allowSideways=*/false), 0);
  EXPECT_EQ(hold(d, 90, now, 1500), 0);
  EXPECT_EQ(hold(d, 90, now, 600), 1);
  EXPECT_EQ(hold(d, 0, now, 2100), 3);  // back the other way
}

TEST(AutoRotateDetector, MovingOrLyingFlatRestartsTheHold) {
  AutoRotateDetector d;
  unsigned long now = 0;
  hold(d, 0, now, 200);
  d.anchorToCurrentPosition();
  EXPECT_EQ(hold(d, 180, now, 700), 0);
  EXPECT_EQ(hold(d, 180, now, 100, true, /*flat=*/80), 0);
  EXPECT_EQ(hold(d, 180, now, 700), 0);  // timer restarted
  EXPECT_EQ(hold(d, 180, now, 400), 2);
}

TEST(AutoRotateDetector, PageTurnDuringATurnDoesNotCancelIt) {
  AutoRotateDetector d;
  unsigned long now = 0;
  hold(d, 0, now, 200);
  d.anchorToCurrentPosition();
  EXPECT_EQ(hold(d, 180, now, 500), 0);
  d.anchorToCurrentPosition();  // ignored: an anchor already exists
  EXPECT_EQ(hold(d, 180, now, 600), 2);
}

TEST(AutoRotateDetector, ResetForgetsTheAnchor) {
  AutoRotateDetector d;
  unsigned long now = 0;
  hold(d, 0, now, 200);
  d.anchorToCurrentPosition();
  d.reset();
  EXPECT_EQ(hold(d, 180, now, 3000), 0);
}

TEST(ReaderAutoRotate, FlipsAreAlwaysTheOppositeOrientation) {
  for (uint8_t o = 0; o < 4; ++o) {
    EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(o, 2, false), (o + 2) % 4);
    EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(o, 2, true), (o + 2) % 4);
  }
}

TEST(ReaderAutoRotate, SidewaysFollowsTheX3SensorFrame) {
  // Portrait "up" is +X; a CCW quarter turn puts +Y up, which is landscape CCW.
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(PORTRAIT, 1, false), LANDSCAPE_CCW);
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(PORTRAIT, 3, false), LANDSCAPE_CW);
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(LANDSCAPE_CCW, 1, false), INVERTED);
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(LANDSCAPE_CW, 1, false), PORTRAIT);
  // Mirrored (learned after a by-hand correction) goes the other way.
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(PORTRAIT, 1, true), LANDSCAPE_CW);
  EXPECT_EQ(ReaderAutoRotate::orientationAfterTurns(PORTRAIT, 0, false), PORTRAIT);
}
