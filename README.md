<div align="center">
     
# 🏓 1D-Ping Pong

**A Real-Time Arcade Physics Game on WS2812B LED Strip**

**by Aniket Chowdhury (aka `#Hashtag`)**

![Status](https://img.shields.io/badge/Status-Stable-brightgreen?style=for-the-badge&logo=arduino)
![Platform](https://img.shields.io/badge/Platform-Arduino%20Uno-blue?style=for-the-badge)
![Type](https://img.shields.io/badge/Type-Embedded%20Arcade%20Game-orange?style=for-the-badge)
![Players](https://img.shields.io/badge/Players-2-red?style=for-the-badge)

</div>

---

# 🎬 PROJECT OVERVIEW

1D LED PONG is a fully hardware-based arcade game built on Arduino Uno and WS2812B LED strip.

A single LED represents a ball moving between two players. Timing, reaction, and strategy decide who wins each rally.

🏓 First to reach the winning score wins the match.

This is not a demo — it is a complete embedded **game engine**.

---

# ⚡ CORE IDEA

LED Strip = Game Field  
LED Dot = Ball  
Zones = Player reaction areas  
Buttons = Physical controls  
FSM = Game engine brain  

---

# 🎮 GAMEPLAY LOOP

SERVE → BALL MOVEMENT → ZONE ENTRY → PLAYER ACTION  
→ HIT / POWER → SPEED CALCULATION  
→ RETURN / MISS → POINT SYSTEM  
→ NEXT SERVE → CONTINUES  

---

# 🧠 WHAT MAKES THIS SPECIAL

✔ Full Finite State Machine (FSM) engine  
✔ Real-time LED physics simulation  
✔ Competitive 2-player arcade system  
✔ Power-based risk/reward mechanics  
✔ Dynamic difficulty scaling  

---

# ⚙️ GAME STATES

- ST_IDLE → Rainbow demo mode  
- ST_START_L / R → First serve system  
- ST_MOVE_LR / RL → Ball movement  
- ST_ZONE_L / R → Reaction zone logic  
- ST_POINT_L / R → Scoring animation  
- ST_RESUME_L / R → Re-serve system  
- ST_WIN_L / R → Endgame state  

---

# ⚡ POWER SYSTEM

| Input | Result |
|------|--------|
| Hit only | Normal return |
| Power only | Random-speed return |
| Hit + Power | BOOSTED return + zone shrink |

Effects:
- Unpredictable gameplay
- Strategy-based risk system
- Progressive difficulty increase

---

# 🏆 SCORING SYSTEM

- Miss = opponent gets point
- Score displayed on LED bar + TM1637 displays
- First to WIN_POINTS wins

---

# 🎨 VISUAL SYSTEM

- 🌈 Idle rainbow animation
- 🟡 Moving ball with trail
- 🟦 Player zones (cyan)
- 🔴 / 🟢 score bars
- ⚡ hit flashes
- 🏁 win animation wipe

---

# 🔊 SOUND SYSTEM

- Bounce tone → normal hit
- Power tone → special action
- Tick sound → movement rhythm
- Score beep → point scored
- Win jingle → game end

---

# 🔌 HARDWARE

Components:
- Arduino Uno
- WS2812B LED Strip (60 LEDs recommended)
- TM1637 Displays (x2)
- 4 Push Buttons
- Passive buzzer
- External 5V supply

---

# ⚡ PIN CONFIG

LED Strip → D2  
Buzzer → D3  
P1 Hit → D4  
P1 Power → D5  
P2 Hit → D6  
P2 Power → D7  
TM1637 P1 → D8 / D9  
TM1637 P2 → D10 / D11  

---

## 📸 Simulation

<img width="1069" height="599" alt="image" src="https://github.com/user-attachments/assets/0ccab762-d627-453a-9a86-c5d82eeede62" />
.
<img width="1055" height="615" alt="image" src="https://github.com/user-attachments/assets/0c560d6a-40b8-4330-968b-bcbb34735fb5" />
.
<img width="1049" height="613" alt="image" src="https://github.com/user-attachments/assets/91bda2c1-875b-4162-929c-2f669b403c74" />


---

## 📸 Wiring & Schematic

<img width="1333" height="825" alt="image" src="https://github.com/user-attachments/assets/7f2af8bb-22a1-48e3-818e-6accae7fc4d9" />
.
<img width="1254" height="840" alt="image" src="https://github.com/user-attachments/assets/b486039f-a989-4d04-95b4-52c478aeed66" />
.

---

# 🧠 ENGINEERING CONCEPTS

- Finite State Machines (FSM)
- Event-driven architecture
- Real-time loop control
- Button debouncing
- Embedded physics simulation
- LED rendering system
- Non-blocking audio system

---

# ⚡ KEY INNOVATION

Most LED games fail due to:

❌ Delay-based logic  
❌ No structure  
❌ No physics system  

This project solves it using:

✔ FSM-driven architecture  
✔ Event-based input system  
✔ Modular game engine design  
✔ Independent physics + rendering layers  

---

# 🎮 GAME MODES

🌈 IDLE MODE → rainbow + demo  
🏓 ACTIVE MODE → full gameplay  
🏁 WIN MODE → animation + jingle  

---

# 🔥 ADVANCED FEATURES

- Power-hit randomness engine
- Dynamic zone shrinking
- Anti-button-mash lockout
- Dual display synchronization
- Forfeit system (10 sec hold both buttons)
- Non-blocking buzzer system
- Speed scaling physics

---

# 🚀 FUTURE UPGRADES

- ESP32 Bluetooth control
- Mobile app controller
- Web scoreboard dashboard
- AI opponent mode
- Adaptive difficulty system
- Tournament mode

---

## 👤 Author & Contact

👨 **Name:** Aniket Chowdhury (aka Hashtag)  
📧 **Email:** [micro.aniket@gmail.com](mailto:micro.aniket@gmail.com)  
💼 **LinkedIn:** [itzz-hashtag](https://www.linkedin.com/in/itzz-hashtag/)  
🐙 **GitHub:** [itzzhashtag](https://github.com/itzzhashtag)  
📸 **Instagram:** [@itzz_hashtag](https://instagram.com/itzz_hashtag)

---

# ⭐ SUPPORT

If you like this project:
⭐ Star the repo  
🍴 Fork it  
🔧 Modify it  
🚀 Build your own version  

---

# 🏁 FINAL THOUGHT

This is not just a game —  
it is a full embedded **arcade engine built from scratch**, combining:

- Physics
- Electronics
- Game design
- Embedded systems
