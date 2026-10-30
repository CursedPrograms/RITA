<p align="center"><img src="images/avatar_round.png" width="160" alt="RITA"></p>

<h1 align="center">RITA</h1>
<h3 align="center">Remote Interface for Total Autonomy</h3>
<p align="center"><i>A DREAM Robotics Remote</i></p>

<div align="center">

[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

</div>

<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" width="180"/>
  </a>
</div>

<div align="center">
  <img alt="C++" src="https://img.shields.io/badge/c++%20-%23323330.svg?&style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
  <img alt="Arduino" src="https://img.shields.io/badge/-Arduino-323330?style=for-the-badge&logo=arduino&logoColor=white"/>
  <img alt="ESP32" src="https://img.shields.io/badge/ESP32-%23323330.svg?&style=for-the-badge&logo=espressif&logoColor=white"/>
  <img alt="Git" src="https://img.shields.io/badge/git%20-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
</div>

---

- Type: Handheld remote (RIFT in your hand)

RITA is a 3.5" touch screen that shows the DREAM Robotics fleet and talks to it through [RIFT](https://github.com/CursedPrograms/RIFT): every robot online, what each one says about itself, and a button for everything it can do.

---

## Related Projects


<table>
  <tr>
    <td align="center"><a href="https://github.com/CursedPrograms/ARIA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/ARIA-Robot-v00/main/images/avatar_round.png" width="72" alt="ARIA"><br>ARIA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/KIDA-Robot-v01"><img src="https://raw.githubusercontent.com/CursedPrograms/KIDA-Robot-v01/main/images/avatar_round.png" width="72" alt="KIDA-01"><br>KIDA-01</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/KIDA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/KIDA-Robot-v00/main/images/avatar_round.png" width="72" alt="KIDA-00"><br>KIDA-00</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/WHIP-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/WHIP-Robot-v00/main/images/avatar_round.png" width="72" alt="WHIP"><br>WHIP</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/NORA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/NORA-Robot-v00/main/images/avatar_round.png" width="72" alt="NORA"><br>NORA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/MILA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/MILA-Robot-v00/main/images/avatar_round.png" width="72" alt="MILA"><br>MILA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/NIKA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/NIKA-Robot-v00/main/images/avatar_round.png" width="72" alt="NIKA"><br>NIKA</a></td>
  </tr>
  <tr>
    <td align="center"><a href="https://github.com/CursedPrograms/IDA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/IDA-Robot-v00/main/images/avatar_round.png" width="72" alt="IDA"><br>IDA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/ARM-Robot-v01"><img src="https://raw.githubusercontent.com/CursedPrograms/ARM-Robot-v01/main/images/avatar_round.png" width="72" alt="ARM"><br>ARM</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/DREAM"><img src="https://raw.githubusercontent.com/CursedPrograms/DREAM/main/images/avatar_round.png" width="72" alt="DREAM"><br>DREAM</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/RIFT"><img src="https://raw.githubusercontent.com/CursedPrograms/RIFT/main/images/avatar_round.png" width="72" alt="RIFT"><br>RIFT</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/NINA"><img src="https://raw.githubusercontent.com/CursedPrograms/NINA/main/images/avatar_round.png" width="72" alt="NINA"><br>NINA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/TINA"><img src="https://raw.githubusercontent.com/CursedPrograms/TINA/main/images/avatar_round.png" width="72" alt="TINA"><br>TINA</a></td>
  </tr>
</table>

---

## Overview

<details open>
<summary><b>Overview</b></summary>

### What she does
- [x] **Splash**: her TV-head avatar for 5 s at power-up.
- [x] **Home**: every robot she drives, its round avatar with its name under it: WHIP, NORA, MILA, NIKA, KIDA-00, KIDA-01, IDA, ARM and ARIA. The ones on the network have a green ring; the rest are dimmed.
- [x] **A controller for each robot**: tap one and drive it, talking to the robot directly (its own web API, the one its PC controller uses). Hold a pad button to drive, let go and it stops. Each robot gets its own pad and buttons:
  - **WHIP** (hexapod): W / A / S / D with curves, crab left / right, HELLO, DANCE, REST, AUTO, CRAB - / + (his crab offset, a degree a press).
  - **ARIA** (drone): her state, battery and network, and the fleet's phrases. No flying: she flies from her app or a gamepad.
  - **NORA** (mecanum): A / D slide, diagonals slide, TURN L / R, AUTO, UV, music, her wheel lock.
  - **MILA, NIKA, KIDA-00, KIDA-01** (tracks): **WASD** or **TANK**, switched at the top. The controls change, and so does the robot's own drive mode. The screen takes one finger at a time, so in TANK the tracks latch: tap the left track's FWD / STOP / BACK, then the right's, and each keeps going until you change it. LINK moves both together; the big STOP stops both.
  - **IDA** (no WiFi): through NORA's IR link.
  - **ARM** (no WiFi): driven from her PC over USB, so RITA just says so.
- [x] **RIFT too**: from a controller, RIFT: ASK / DO shows what the robot says about itself through RIFT and everything RIFT can do with it, as 1 s timed moves that ask *"Let RITA move NORA?"* first.
- [x] **Safety stays with the robots**: their own stops (walls, tilt, the wheel locks) still hold. Their PIN (`ROBOT_PIN` in `secrets.h`) signs RITA in as a controller; unlocking wheels is its own button (UNLOCK).
- [x] **The fleet's WiFi ladder**: she joins the highest network she can see (RIFT, DREAM, NINA, TINA, the controllers, NORA), looks again every minute, and starts over when hers goes quiet. She registers with the hub as `RITA` (type `remote`).
- [x] **LED**: green on RIFT's network, amber on another one (the list works, ask and do need RIFT), red with no network.

### Later
- [ ] Host the fleet herself when there's no RIFT (a place on the WiFi ladder).
- [ ] RIFT's music together, the mission log and the robots' conversations.
- [ ] Start a robot's MIND mode from her (through its PC controller).

</details>

---

## Prerequisites

<details>
<summary><b>Hardware</b></summary>

| Part | Notes |
| :--- | :--- |
| **ESP32-3248S035R** | The 3.5" "Cheap Yellow Display": ESP32, 480x320 ST7796 screen, XPT2046 resistive touch, RGB LED, SD slot, speaker connector |
| **USB cable** | Power and flashing (micro-USB or USB-C, depending on the board revision) |

</details>

<details>
<summary><b>Software</b></summary>

- [Arduino IDE](https://docs.arduino.cc/software/ide/) (or arduino-cli) with the **esp32** board package (3.x)
- Libraries: **LovyanGFX**, **ArduinoJson** (7.x)

</details>

---

## Pinouts

<details>
<summary><b>ESP32-3248S035R</b></summary>

| Part | Pins |
| :--- | :--- |
| ST7796 screen (HSPI) | SCLK 14, MOSI 13, MISO 12, DC 2, CS 15, backlight 27 |
| XPT2046 touch (same bus) | CS 33, IRQ 36 |
| RGB LED (active low) | R 4, G 16, B 17 |

Negative colours? Flip `PANEL_INVERT` in `scripts/esp32/display.h`. Red and blue swapped? `PANEL_BGR`.

</details>

---

## How to Run

<details open>
<summary><b>View How to Run</b></summary>

1. Copy `scripts/esp32/secrets.example.h` to `secrets.h` and put in the fleet's WiFi passwords (the same as every robot's).
2. Flash her: `flash.bat` (on a fresh PC it installs the Arduino command line tool, the ESP32 board package and her libraries by itself, then finds the port, compiles and flashes, talking you through it), or open `scripts/esp32/esp32.ino` in the Arduino IDE with **ESP32 Dev Module** and the **Huge APP** partition scheme.
3. On the first boot, tap each corner as it lights up (touch calibration, kept in flash). Hold the screen while she starts to calibrate again.
4. She finds the fleet's network by herself. Tap a robot to see it and drive it.
</details>

---

<div align="center">&copy; Cursed Entertainment 2026</div>

<div align="center">
  <a href="https://cursed-entertainment.itch.io/">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png" alt="Cursed Entertainment" width="250"/>
  </a>
</div>

---

<!-- DREAM-ECOSYSTEM:START -->
## The DREAM Robotics ecosystem

**Minds** — [DREAM](https://github.com/CursedPrograms/DREAM) companion · [TINA](https://github.com/CursedPrograms/TINA) engineer · [NINA](https://github.com/CursedPrograms/NINA) caretaker · [RIFT](https://github.com/CursedPrograms/RIFT) fleet hub · [LYCEA](https://github.com/CursedPrograms/LYCEA) training school  
**Robots** — [NORA](https://github.com/CursedPrograms/NORA-Robot-v00) · [MILA](https://github.com/CursedPrograms/MILA-Robot-v00) · [WHIP](https://github.com/CursedPrograms/WHIP-Robot-v00) · [KIDA-00](https://github.com/CursedPrograms/KIDA-Robot-v00) · [KIDA-01](https://github.com/CursedPrograms/KIDA-Robot-v01) · [IDA](https://github.com/CursedPrograms/IDA-Robot-v00) · [ARM](https://github.com/CursedPrograms/ARM-Robot-v01) · [NIKA](https://github.com/CursedPrograms/NIKA-Robot-v00) · [ARIA](https://github.com/CursedPrograms/ARIA-Robot-v00)  
**Remotes** — [RITA](https://github.com/CursedPrograms/RITA) handheld remote  
<!-- DREAM-ECOSYSTEM:END -->
