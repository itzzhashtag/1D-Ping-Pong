# 🏓 1D LED PONG – Arduino Uno Edition (v1.7)

A fully hardware-driven **1D Pong arcade game** built using Arduino Uno, WS2812B LED strip, TM1637 displays, and a custom finite state machine (FSM) engine.

This project transforms a simple LED strip into a competitive 2-player arcade game with physics-like movement, power mechanics, scoring, and real-time feedback.

---

# 🎯 GAME OVERVIEW

Two players face off on a linear LED strip.

The “ball” travels left ↔ right.
Players must return it using timing + strategy:

### 🎮 Controls per player
- **HIT button** → normal return
- **POWER button** → special mechanic (risk/reward system)

---

# ⚡ GAME MECHANICS

## 🟢 1. Serve System
- Hit → normal serve
- Power → BLOCKED during:
  - First serve
  - Re-serve after point

---

## 🔵 2. Rally System (core gameplay)

When the ball enters a player's zone:

| Input | Result |
|------|--------|
| Hit only | Normal speed return |
| Power only | Random speed return |
| Hit + Power | BOOSTED return + zone shrink |

✔ Boost makes ball faster  
✔ Zone shrink increases difficulty per rally  

---

## 🟡 3. Scoring System

- Miss = opponent scores
- First to `WIN_POINTS` wins
- Score shown on dual TM1637 displays

---

## 🔴 4. Win / End Game

When a player wins:
- LED animation plays
- Victory jingle plays
- Displays show:
  - Winner → "WOn"
  - Loser → "dEAd"

---

## 🔥 5. Advanced Features

### 🎯 FSM-Based Engine
Fully structured state machine:
- START
- MOVE
- ZONE
- POINT
- RESUME
- WIN
- IDLE

---

### ⚡ Power System
- Randomized speed generation
- Boost multiplier (faster returns)
- Strategic risk mechanic

---

### 🎨 Visual System
- WS2812B LED strip:
  - Ball movement with trail
  - Cyan hit zones
  - Dynamic score bars
  - Idle rainbow animation

---

### 🔊 Sound System
- Bounce tones
- Power-hit tones
- Score tones
- Win jingle sequence

---

### ⛔ Forfeit System
Hold BOTH buttons (Hit + Power) for 10 seconds:
- Player instantly forfeits
- Opponent wins

---

# 🔌 HARDWARE CONNECTIONS

## 🧩 Arduino Uno Wiring

### WS2812B
- DIN → Pin 2

### Buzzer
- Signal → Pin 3

### Player 1
- Hit → Pin 4
- Power → Pin 5

### Player 2
- Hit → Pin 6
- Power → Pin 7

### TM1637 Displays
- P1 CLK → Pin 8
- P1 DIO → Pin 9
- P2 CLK → Pin 10
- P2 DIO → Pin 11

---

# 📦 LIBRARIES REQUIRED

Install via Arduino Library Manager:

- Adafruit NeoPixel
- TM1637Display (by avishorp)

---

# ⚙️ CONFIGURATION HIGHLIGHTS

```cpp
#define NPIXELS 60
#define ZONE_SIZE 7
#define WIN_POINTS 9
