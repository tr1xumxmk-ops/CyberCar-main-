// =============================================================================
// config.h — CYBER CAR PRO — Global constants, helpers, enums
// =============================================================================
#pragma once
#include <stdint.h>

namespace Config {
constexpr char AP_SSID[]     = "Cyber-Car-Pro";
constexpr char AP_PASSWORD[] = "12345678";

constexpr uint8_t MOTOR_IN1_PIN = 7;
constexpr uint8_t MOTOR_IN2_PIN = 6;
constexpr uint8_t MOTOR_IN3_PIN = 4;
constexpr uint8_t MOTOR_IN4_PIN = 5;
constexpr uint8_t MOTOR_ENA_PIN = 11;
constexpr uint8_t MOTOR_ENB_PIN = 12;

constexpr uint8_t ULTRASONIC_TRIG_PIN = 1;
constexpr uint8_t ULTRASONIC_ECHO_PIN = 2;
constexpr uint8_t SERVO_SCAN_PIN = 10;
constexpr uint8_t SERVO_BALL_PIN = 13;

constexpr uint8_t LED_RED_PIN   = 8;
constexpr uint8_t LED_GREEN_PIN = 9;
constexpr uint8_t LED_BLUE_PIN  = 3;
constexpr uint8_t BUZZER_PIN    = 18;
constexpr uint8_t BATTERY_PIN   = 34;

constexpr uint16_t SAFE_DISTANCE_CM     = 25;
constexpr uint16_t CRITICAL_DISTANCE_CM = 15;
constexpr uint8_t BALL_LOCKED_ANGLE     = 0;
constexpr uint8_t BALL_RELEASE_ANGLE    = 90;

constexpr uint16_t SENSOR_INTERVAL_MS   = 65;
constexpr uint16_t SENSOR_STALE_MS      = 300;
constexpr uint16_t MANUAL_TIMEOUT_MS    = 700;
constexpr uint16_t AUTO_REVERSE_MS      = 240;
constexpr uint16_t AUTO_SCAN_SETTLE_MS  = 330;
constexpr uint16_t AUTO_TURN_MS         = 360;
constexpr uint16_t AUTO_U_TURN_MS       = 700;
constexpr uint16_t RL_ACTION_MS         = 180;
constexpr uint16_t RL_TURN_MS           = 150;
constexpr uint16_t RL_REVERSE_MS        = 210;
constexpr uint16_t DNS_PORT             = 53;
constexpr uint32_t ULTRASONIC_TIMEOUT_US = 25000;

constexpr float ADC_REFERENCE_V       = 3.3f;
constexpr float BATTERY_DIVIDER_RATIO = 3.12f;
constexpr float BATTERY_EMPTY_V       = 6.4f;
constexpr float BATTERY_FULL_V        = 8.4f;
constexpr uint16_t BATTERY_ADC_MAX    = 4095;

constexpr uint8_t SENSOR_FILTER_SAMPLES = 3;
constexpr uint8_t TOKEN_HEX_LENGTH      = 32;
}

static_assert(Config::CRITICAL_DISTANCE_CM < Config::SAFE_DISTANCE_CM,
              "Critical distance must be lower than safe distance.");

static inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}
static inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

enum class DriveCommand : uint8_t { Stop, Forward, Backward, Left, Right, Brake };
enum class OperatingMode : uint8_t { Manual, Autonomous, ReinforcementLearning };