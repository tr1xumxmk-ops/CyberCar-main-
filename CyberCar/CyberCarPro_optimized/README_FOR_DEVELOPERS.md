# CYBER CAR PRO — Developer Guide / دليل المطورين

**Project status:** educational prototype. The project owner's intent is to keep it private and limit use to education, research, and supervised demonstrations. See `EDUCATIONAL_USE_NOTICE.md`: the supplied MIT license does not itself impose that restriction.

## 1. Architecture / البنية العامة

The supplied team notes describe a single-responsibility layout:

| File | Intended responsibility |
|---|---|
| `config.h` | Shared constants, configuration, and helper values |
| `image_data.h` | Embedded bitmap bytes only |
| `bmp_resource.h` | BMP header and image response formatting |
| `ai_module.h` | Quantized MLP, Double Q-learning, and navigation logic |
| `captive_portal.h` | DNS/captive-portal behavior, HTTP routes, and dashboard UI |
| `CyberCarPro_optimized.ino` | Arduino entry point and module integration |

The supplied documentation lists these files; compare the list with the actual repository before relying on it.

## 2. Configuration / الإعدادات

The team guide identifies values such as:

- `SAFE_DISTANCE_CM = 25`: nominal threshold below which forward movement should be blocked.
- `CRITICAL_DISTANCE_CM = 15`: nominal critical-distance threshold.
- `MANUAL_TIMEOUT_MS = 700`: nominal manual-control command timeout.

Keep safety parameters centralized, document their physical units, and validate sensor readings before using them. These values are descriptions from the supplied guide, not proof that they are correct for every chassis or stopping distance.

## 3. Bitmap resource / مورد الصورة

The team guide describes a 1408 × 768, one-bit image:

- Bytes per row: 1408 ÷ 8 = 176.
- Pixel data: 176 × 768 = 135,168 bytes.
- The BMP header is described as 62 bytes.

The browser needs a complete image format, not raw pixel bytes. The documented response sends the header and image bytes. Chunked writes of approximately 1 KB are described as a way to keep the ESP32 responsive and avoid long blocking operations. Verify the header fields, row orientation, byte order, response length, and watchdog behavior against the actual implementation.

## 4. Quantized neural network / الشبكة العصبية الكمية

The described policy network has 5 inputs, 8 hidden units, and 3 outputs (`5 → 8 → 3`). Inputs listed in the guide are front/right/left distances, approach rate, and current speed. Outputs represent forward/left/right scores.

The guide reports `int8` weights and fixed-point scaling. Fixed-point arithmetic saves memory and can reduce computational cost on microcontrollers, but its scale, rounding, saturation, and signed-shift behavior must be tested. A neural network must never override a verified hard safety interlock. The guide says the model is bypassed below a nominal forward-distance threshold; confirm this check happens before the motor command is applied.

## 5. Reinforcement learning / التعلم المعزز

The supplied description calls the agent `QuantizedDoubleQLearning` and reports:

- 8 states: four distance zones multiplied by two approach trends.
- 5 actions: forward, left, right, reverse, and brake.
- A reward computed after approximately 180 ms.
- Two Q-tables (Double Q-learning) to reduce overestimation bias.
- A circular replay buffer holding 24 transitions.
- Epsilon-greedy exploration decaying from 50% to 5%.
- A short sensor history for estimating approach rate.
- NVS persistence approximately every 20 seconds.

These are reported design details. Check the actual reward function, terminal conditions, random update logic, episode boundaries, replay sampling, and NVS schema. Persistence code should handle missing, corrupt, and older-version data without issuing motor commands unexpectedly.

### Safety review checklist

1. Ensure invalid/timeout sensor readings map to a conservative state.
2. Test obstacles at different distances, angles, and surface materials.
3. Test that a lost control link stops motion within the required time.
4. Add a physical stop or power-disconnect path independent of the learned policy.
5. Keep exploratory learning disabled or tightly constrained during first hardware tests.
6. Log state/action/reward sequences and replay them in simulation before changing the reward function.

## 6. Navigation modes / أوضاع الحركة

The supplied project README describes three modes:

- **Manual:** joystick control with an automatic timeout and distance-based forward-motion safety check.
- **Auto:** forward motion, reverse on blockage, left/right scan, then movement toward the clearer side; a turn-around fallback is described.
- **RL-AI:** epsilon-greedy action selection and online Q-learning, with telemetry for updates, epsilon, best reward, and episodes.

Use a mode-transition table or explicit state machine. On mode change, sensor failure, authentication failure, or watchdog reset, default to motor-off until conditions are revalidated.

## 7. Captive portal / بوابة الاتصال

The project README reports a local Wi-Fi AP, DNS captive portal, HTTP dashboard, session token, rate limiting, and CSP. In the actual source, review:

- Authentication on every state-changing route.
- Session-token lifetime, rotation, storage, invalidation, and log exposure.
- Cookie flags; remember that `HttpOnly` prevents JavaScript access but does not encrypt HTTP traffic.
- CSRF/origin checks, request-size limits, and input range validation.
- Rate-limiting behavior when requests arrive concurrently or wrap-around timers are used.
- CSP compatibility with any inline scripts/styles and third-party font requests.
- Safe behavior after reboot, AP restart, or client disconnection.

A captive portal is a convenience interface, not a strong security boundary. Use a unique password and avoid exposing it outside a supervised demo.

## 8. Hardware and pin validation / مراجعة العتاد

The supplied pin table assigns GPIO 6, 7, 8, 9, 10, and 11 to peripherals. On many ESP32 boards, GPIO 6–11 are used by flash and should not be treated as generic GPIO. Check the exact ESP32 variant and board schematic before wiring. Also level-shift the HC-SR04 ECHO pin where necessary, share grounds correctly, and provide adequate motor/servo power that will not brown out the MCU.

Never consider the pin list a wiring prescription until it is confirmed for the specific board.

## 9. Build and release workflow

1. Keep configuration, UI, learning logic, and image bytes separated.
2. Compile on a clean checkout with the intended board package and libraries.
3. Run static checks and any available unit tests before hardware tests.
4. Test motors disconnected, then wheels raised, then in a clear low-speed area.
5. Record the board revision, pin mapping, dependencies, firmware hash, and test results.
6. Do not commit passwords, tokens, Wi-Fi credentials, private keys, or personally identifying logs.
7. Keep the repository private and restrict collaborator access according to the project owner's policy.

## 10. شرح عربي مختصر

- `config.h`: الإعدادات المشتركة والمسافات وحدود الأمان.
- `image_data.h` و`bmp_resource.h`: بيانات الصورة ورأس BMP وطريقة إرسالها للمتصفح.
- `ai_module.h`: الشبكة العصبية الصغيرة والتعلم المعزز ومنطق اختيار الحركة.
- `captive_portal.h`: شبكة الاتصال المحلية وصفحات التحكم ومسارات HTTP.
- ملف `.ino`: نقطة البداية وربط المكونات.

**تنبيه:** لا تستخدم السيارة قرب الأشخاص أو السلالم أو الطرق العامة. اختبر من دون تشغيل المحركات أولاً، ثم ارفع العجلات عن الأرض، ووفّر إيقافاً مادياً مستقلاً. الشبكة العصبية والتعلم المعزز قد ينتجان قرارات خاطئة.

## Source basis

This guide is based on the two supplied Markdown files, especially the original team walkthrough. It describes the documented design; it does not claim that the unseen/actual firmware has been independently audited or tested.

## Official hardware reference

For ESP32 family pin limitations, start with the [Espressif ESP-IDF GPIO reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html) and the datasheet/schematic for the exact module. Pin assignments differ across ESP32 variants and carrier boards.
