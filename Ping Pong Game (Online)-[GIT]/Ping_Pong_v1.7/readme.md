<div align="center">

# 🏓⚡ 1D LED PONG – Arduino Physics Arcade Game

**A Real-Time 1D Physics-Based Pong Game using Arduino + WS2812B LED Strip**

**by Aniket Chowdhury (aka `#Hashtag`)**

<img src="https://img.shields.io/badge/Status-Stable-brightgreen?style=for-the-badge&logo=arduino" />
<img src="https://img.shields.io/badge/Platform-Arduino%20Uno%20%7C%20AVR%20%7C%20ESP32-blue?style=for-the-badge" />
<img src="https://img.shields.io/badge/Type-Embedded%20Physics%20Game-orange?style=for-the-badge" />
<img src="https://img.shields.io/badge/Players-2-red?style=for-the-badge" />

</div>

---

# 🎬 Overview

**1D LED Pong** is a fast-paced embedded arcade game where a single LED represents a moving “ball” across a strip.

Players compete using **Hit** and **Power buttons** to return the ball before it reaches their end.

This is NOT a simple LED game — it is a **real-time physics simulation running on Arduino**.

---

# ⚡ Core Concept

Each frame simulates:

- 📍 Position (LED index)
- ⚡ Velocity (speed per step)
- 🎯 Zone interaction
- 🔁 Collision & return physics
- 🧠 Input-based state transitions

> The LED strip behaves like a 1D physics world.

---

# 🎮 Gameplay Mechanics

## 🟢 Controls

| Input | Action |
|------|--------|
| HIT | Normal return |
| POWER | Special behavior |
| HIT + POWER | BOOST return |

---

## ⚙️ Game Rules

- First to **WIN_POINTS (default 9)** wins
- Ball must be returned before reaching wall
- Missing = opponent scores
- Zone determines reaction area
- Speed increases dynamically

---

## 🚀 Special Mechanics

### ⚡ Power System
- Random speed return
- Unpredictable timing
- Anti-pattern gameplay

### 🔥 Boost System
- HIT + POWER together
- Faster ball return (×0.75 time)
- Shrinks opponent zone permanently

### 🎯 Zone System
- Each player has a reaction zone
- Shrinks when boosting
- Adds difficulty scaling mid-match

---

# 🔌 Hardware Setup

## 🧰 Components

- Arduino Uno / Nano / ESP32
- WS2812B LED Strip (60 LEDs recommended)
- TM1637 4-digit displays ×2
- Passive buzzer
- Push buttons ×4
- External 5V power supply

---

## ⚡ Wiring

| Component | Pin |
|----------|-----|
| LED Strip Data | D2 |
| Buzzer | D3 |
| P1 HIT | D4 |
| P1 POWER | D5 |
| P2 HIT | D6 |
| P2 POWER | D7 |
| TM1637 #1 CLK | D8 |
| TM1637 #1 DIO | D9 |
| TM1637 #2 CLK | D10 |
| TM1637 #2 DIO | D11 |

⚠️ LED strip MUST use external 5V power

---

# 📦 Required Libraries

Install via Arduino Library Manager:

- Adafruit NeoPixel
- TM1637Display (by Avishorp)

---

# 🎮 Game Flow

```text
🌈 IDLE MODE (Rainbow Animation)
        ↓
🎮 PLAYER INPUT (Hit / Power)
        ↓
🚀 SERVE STATE
        ↓
⚡ BALL MOVEMENT (Physics Engine)
        ↓
🎯 ZONE INTERACTION
        ↓
🔁 RETURN or MISS
        ↓
📊 SCORE UPDATE
        ↓
🏆 WIN CHECK
        ↓
🎆 WIN ANIMATION
        ↓
🔄 RESET / IDLE LOOP

```

# 🧠 State Machine

- ST_IDLE → Waiting mode  
- ST_START_L / ST_START_R → Serve phase  
- ST_MOVE_LR / ST_MOVE_RL → Ball moving  
- ST_ZONE_L / ST_ZONE_R → Reaction zone  
- ST_POINT_L / ST_POINT_R → Score animation  
- ST_RESUME_L / ST_RESUME_R → Re-serve phase  
- ST_WIN_L / ST_WIN_R → Game over  

---

# 🔊 Sound System

| Event      | Sound         |
|------------|--------------|
| Hit        | Bounce tone  |
| Power Hit  | High tone    |
| Movement   | Tick sound   |
| Score      | Alert tone   |
| Serve      | Start tone   |
| Win        | Full melody 🎵 |

---

# 🌈 LED Visualization

| Element         | Color            |
|----------------|-----------------|
| Ball           | 🟡 Yellow trail  |
| Player 1 Zone  | 🔵 Cyan         |
| Player 2 Zone  | 🔵 Cyan         |
| Score P1       | 🔴 Red          |
| Score P2       | 🟢 Green        |
| Idle           | 🌈 Rainbow       |

---

# ⚙️ Key Features

✔ Real-time physics simulation  
✔ Advanced FSM architecture  
✔ Button debounce system  
✔ Boost + power mechanics  
✔ Zone-based gameplay  
✔ Dynamic speed scaling  
✔ LED score system  
✔ Win animations + sound  
✔ Idle animation engine  
✔ Anti-spam lockout system  
✔ Forfeit detection (hold both buttons 10s)  

---

# 🧠 Engineering Concepts

- Finite State Machine (FSM)  
- Embedded physics simulation  
- Non-blocking timing system  
- Event bitmask system  
- Real-time LED rendering  
- Debounce filtering  
- Multi-layer input detection  
- Performance optimized Arduino loop  

---

# 🚀 Advanced Mechanics

## ⚡ Power Hit
- Randomized speed  
- Unpredictable returns  
- Breaks timing prediction  

---

## 🔥 Boost Mode
- Hit + Power  
- Faster ball movement  
- Shrinks opponent zone  

---

## ⛔ Forfeit System

Hold BOTH buttons for 10 seconds → instant loss

---

# 🏁 Win Condition

### Triggers:
- 🏆 Win animation  
- 🎵 Victory music  
- 📊 Score freeze  
- 🔄 Restart option  

---

# 📊 Difficulty Scaling

As match progresses:

- Speed increases  
- Zones shrink  
- Reaction time decreases  
- Game becomes more intense automatically  

---

# 🎬 Simulation

👉 Wokwi Project:  
https://wokwi.com/projects/461754054622412801  

---

# 🛠 Future Improvements

- 📱 Mobile Bluetooth control  
- 🌐 Web scoreboard (ESP32)  
- 🤖 AI opponent  
- 🎮 Difficulty selector  
- 🧠 Adaptive physics tuning  
- 🏆 Tournament mode  

---

## 👤 Author & Contact

👨 **Name:** Aniket Chowdhury (aka Hashtag)  
📧 **Email:** [micro.aniket@gmail.com](mailto:micro.aniket@gmail.com)  
💼 **LinkedIn:** [itzz-hashtag](https://www.linkedin.com/in/itzz-hashtag/)  
🐙 **GitHub:** [itzzhashtag](https://github.com/itzzhashtag)  
📸 **Instagram:** [@itzz_hashtag](https://instagram.com/itzz_hashtag)

---

# ⭐ Support

If you like this project:

- ⭐ Star the repo  
- 🍴 Fork it  
- 🔧 Build your own version  
- 🎮 Modify & experiment  

---

# 🔥 Final Thought

> This is not just a game —  
> it is a **real-time embedded physics engine disguised as fun**.

 

```cpp
 
