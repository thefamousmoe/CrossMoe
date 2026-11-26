#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

void HalTiltSensor::begin() {
#ifdef FORCE_TILT_SENSOR_AVAILABLE
  _available = true;
  _isAwake = false;
  _initMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "Tilt sensor override active via build flag");
  return;
#endif

  _available = false;
  if (!_sdkImu.begin()) {
    LOG_ERR("GYR", "SDK IMU not found");
    return;
  }
  // Stand by even accelerometer-only parts; they cannot drive gyro page turns.
  if (!_sdkImu.sleep()) LOG_ERR("GYR", "IMU standby failed");
  _available = _sdkImu.hasGyroscope();
  if (!_available) {
    LOG_INF("GYR", "IMU has no gyroscope");
    return;
  }
  _initMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "SDK IMU initialized");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.wake()) {
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _lastTiltMs = millis();
  _wakeMs = millis();
  _isAwake = true;
  // Entering a book or waking from sleep: adopt the position the reader is
  // held in once it is held still briefly.
  resetAutoRotate();
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  resetAutoRotate();
  _inTilt = false;
  _isAwake = false;
  return true;
}

void HalTiltSensor::setAutoRotate(const uint8_t mode, const unsigned long flipHoldMs,
                                  const unsigned long sidewaysHoldMs) {
  if (mode != _autoRotateMode) resetAutoRotate();
  _autoRotateMode = mode;
  _flipHoldMs = flipHoldMs;
  _sidewaysHoldMs = sidewaysHoldMs;
}

uint8_t HalTiltSensor::takeAutoRotateTurns() {
  const uint8_t turns = _pendingQuarterTurns;
  _pendingQuarterTurns = 0;
  return turns;
}

void HalTiltSensor::update(const uint8_t enabled, const uint8_t direction, const uint8_t orientation,
                           const bool inReader, const uint8_t sensitivity) {
  if (!_available) {
    return;
  }

  const bool tiltEnabled = enabled != CrossPointTiltPageTurn::TILT_OFF;
  const bool autoRotateEnabled = _autoRotateMode != 0;
  const bool shouldBeAwake = (tiltEnabled || autoRotateEnabled) && inReader;

  // State machine: keep the sensor awake only while a reader needs it.
  if (shouldBeAwake && !_isAwake) {
    _isAwake = wake();
    return;
  } else if (!shouldBeAwake && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  // If disabled or outside a reader, skip polling and avoid unnecessary I2C traffic.
  if (!shouldBeAwake) {
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  Imu::Sample sample;
  if (!_sdkImu.read(sample)) {
    return;
  }

  if (tiltEnabled) {
    updateTiltGesture(sample, direction, orientation, sensitivity, now);
  }

  if (autoRotateEnabled) {
    const float gyroDps = fmaxf(fabsf(sample.gx), fmaxf(fabsf(sample.gy), fabsf(sample.gz)));
    const AutoRotateDetector::Sample reading{sample.ax, sample.ay, sample.az, gyroDps, now};
    const uint8_t turns = _autoRotate.update(reading, _autoRotateMode >= 2, _flipHoldMs, _sidewaysHoldMs);
    if (turns != 0) {
      _pendingQuarterTurns = turns;
      LOG_INF("GYR", "Auto-rotate: %u quarter turn(s), accel=(%.2f, %.2f, %.2f)", turns, sample.ax, sample.ay,
              sample.az);
    }
  }
}

void HalTiltSensor::updateTiltGesture(const Imu::Sample& sample, const uint8_t direction, const uint8_t orientation,
                                      const uint8_t sensitivity, const unsigned long now) {
  const float gx = sample.gx;
  const float gy = sample.gy;

  // Map the gyro axis to the selected gesture based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  const bool forwardBack = direction == CrossPointTiltPageTurnDirection::TILT_FORWARD_BACK ||
                           direction == CrossPointTiltPageTurnDirection::TILT_FORWARD_BACK_INVERTED;
  const bool inverted = direction == CrossPointTiltPageTurnDirection::TILT_LEFT_RIGHT ||
                        direction == CrossPointTiltPageTurnDirection::TILT_FORWARD_BACK_INVERTED;
  float tiltAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = forwardBack ? gy : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = forwardBack ? -gy : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = forwardBack ? gx : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = forwardBack ? -gx : gy;
      break;
    default:
      tiltAxis = forwardBack ? gy : gx;
      break;
  }
  if (inverted) {
    tiltAxis = -tiltAxis;
  }

  // Keep Normal's existing board calibration. Low stays below the Sticky's
  // ±245 dps range; High accepts gentler flicks without changing rearm/cooldown.
  float rateThreshold = RATE_THRESHOLD_DPS;
  if (sensitivity == TiltSensitivity::LOW_SENSITIVITY) rateThreshold *= 1.15f;
  if (sensitivity == TiltSensitivity::HIGH_SENSITIVITY) rateThreshold *= 0.75f;

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > rateThreshold) {
        _tiltForwardEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        _autoRotate.interrupt();
        LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
      } else if (tiltAxis < -rateThreshold) {
        _tiltBackEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        _autoRotate.interrupt();
        LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
      }
    }
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}
