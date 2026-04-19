# 🏓 1D LED PONG (Arduino Uno)

A fast-paced, real-time **1D Pong game built on WS2812B LED strip + Arduino Uno**, featuring dual players, power mechanics, zone-based hits, scoring system, sound effects, and animated UI via TM1637 displays.

---

## ⚡ What it is

A physical LED-based Pong game where two players battle using:
- 🎯 Hit button (normal return)
- ⚡ Power button (random/boost mechanics)
- 🟦 Dynamic “hit zones”
- 🔊 Sound feedback system
- 🏆 First to reach target score wins

---

## 🎮 Core Features

- Real-time LED ball physics (left ↔ right movement)
- Power-hit system:
  - Power alone → random speed return
  - Hit alone → normal return
  - Hit + Power → boosted return + shrinking zone
- Dual 7-segment score displays (P1 & P2)
- Win animations + jingle
- Idle rainbow animation mode
- Forfeit system (hold both buttons for 10 sec)
- Fully event-driven FSM architecture

---

## 🔧 Hardware

- Arduino Uno
- WS2812B LED Strip
- TM1637 7-Segment Displays (x2)
- Passive buzzer
- 4 buttons (Hit/Power per player)

---

## 🧠 Status

✔ Fully working FSM-based game engine  
✔ Power-hit bug fixed (standalone power return enabled)  
✔ Competitive arcade-style gameplay  

---

## 🚀 Build it. Play it. Compete.

A physical arcade Pong experience reimagined in LEDs.
