[README.md](https://github.com/user-attachments/files/33789744/README.md)
# CYBER CAR PRO

An autonomous AI-powered robotic car with a secure captive portal, real-time
reinforcement learning, and a parallax cyberpunk web dashboard.

Built by AI & Robotics Engineers at Al-Iraqia University, supervised by the
SPARK Team.

---

## Features

- Autonomous obstacle avoidance (5-phase state machine)
- Reinforcement learning agent with Double Q-learning, experience replay
  and adaptive exploration
- Quantized neural network policy fallback (5 → 8 → 3 MLP)
- Secure captive portal with HttpOnly session tokens, CSP headers,
  per-endpoint rate limiting
- Parallax UI that reacts to scroll, with live telemetry
- Live RL learning telemetry (updates, epsilon, best reward, episodes)
- Persistent learning across reboots via NVS
- Bilingual UI (Arabic / English, full RTL support)
- Watchdog-safe image streaming (1 KB chunks)
- 62-byte BMP header auto-generated at flash time

---

## Hardware

- ESP32 (any variant with WiFi + 4 MB flash)
- L298N motor driver
- 2x DC motors
- HC-SR04 ultrasonic distance sensor
- 2x SG90 or MG90S servos (scan + ball release)
- RGB LED or 3 discrete LEDs
- Passive buzzer
- Battery pack (7.4V LiPo recommended)
- Voltage divider on GPIO 34 for battery reading

---

## Wiring

| GPIO | Function                    |
|------|-----------------------------|
| 7    | Motor IN1                   |
| 6    | Motor IN2                   |
| 4    | Motor IN3                   |
| 5    | Motor IN4                   |
| 11   | Motor ENA (PWM)             |
| 12   | Motor ENB (PWM)             |
| 1    | Ultrasonic TRIG             |
| 2    | Ultrasonic ECHO             |
| 10   | Scan servo signal           |
| 13   | Ball servo signal           |
| 8    | LED red                     |
| 9    | LED green                   |
| 3    | LED blue                    |
| 18   | Buzzer                      |
| 34   | Battery (ADC input)         |

---

## Project structure

<img width="2892" height="1916" alt="wiring_diagram" src="https://github.com/user-attachments/assets/60f038c3-3b79-45f5-8693-d764c2d39150" />


## Quick start

1. **Paste the bitmap.** Open `image_data.h` and paste your 8,448-line
   array where indicated. Keep the exact variable name.

2. **Install libraries** (Library Manager or PlatformIO):
   - `ESP32Servo`
   - `ArduinoJson` (optional, not used but recommended)
   - Standard `WiFi`, `DNSServer`, `WebServer`, `Preferences` (bundled)

3. **Select board.** Arduino IDE → Tools → Board → ESP32 Dev Module.

4. **Compile and upload.** `Sketch → Upload`. ~140 KB will be written.

5. **Connect.**
   - WiFi network: `Cyber-Car-Pro`
   - Password: `12345678`
   - The portal opens automatically on most phones.

6. **Open the dashboard.** `http://192.168.4.1/`

---

## Operating modes

**Manual** — Joystick control. The car moves only while the joystick is being
dragged. A 700 ms timeout stops it automatically if the connection drops.
Distance safety: the car refuses to move forward if the front sensor reads
below the safe distance (25 cm).

**Auto** — Full autonomous navigation. The car drives forward, reverses when
blocked, scans left and right, then picks the more open direction. A
U-turn fallback is used if both sides are blocked.

**RL-AI** — Reinforcement learning. The agent observes the environment,
selects actions using epsilon-greedy policy over a Double Q-table, and
learns from every transition. Exploration decays from 50% to 5%. Episodes
are tracked. Progress is visible in the live telemetry panel.

---

## Security model

- **Session token** — 128-bit random token generated on boot using
  `esp_random()`. Stored only in an HttpOnly, SameSite=Strict cookie.
- **Rate limiting** — Per-endpoint intervals prevent API abuse.
- **Constant-time comparison** — Token validation resists timing attacks.
- **CSP headers** — Restricts script and style sources.
- **No external dependencies** — The UI loads only fonts from Google
  (removable if you want fully offline operation).

To connect a custom controller:

Or send the token as a header: `X-Session-Token: YOUR_TOKEN`

The token is printed to Serial at boot.

---

## Troubleshooting

**Car doesn't move.** Check that the mode is Manual and the joystick is being
dragged. A safety brake may be active — tap any joystick direction to
release it.

**Image doesn't show.** Verify `image_data.h` contains the pasted array with
the correct variable name. If still broken, check Serial output for a
"Watchdog init failed" message.

**Battery reading is wrong.** Adjust `BATTERY_DIVIDER_RATIO` in `config.h` to
match your resistor values.

**Portal doesn't open.** Manually navigate to `http://192.168.4.1/`.
Some phones require disabling mobile data for captive portals to trigger.

---

## License

MIT License

Copyright (c) 2026 Al-Iraqia University, AI & Robotics Department, SPARK Team

NOTICE: This project is strictly intended for educational and academic purposes only. 
It is not authorized for commercial use, resale, or distribution for profit.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice, this permission notice, and the educational use restriction 
above shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
