<div align="center">

# 🏓 1D Ping Pong

**A Real-Time Arcade Physics Game on WS2812B LED Strip**

**by Aniket Chowdhury (aka `#Hashtag`)**

<img src="https://img.shields.io/badge/Status-Stable-brightgreen?style=for-the-badge&logo=arduino" />
<img src="https://img.shields.io/badge/Platform-Arduino%20Uno-blue?style=for-the-badge" />
<img src="https://img.shields.io/badge/Type-Embedded%20Arcade%20Game-orange?style=for-the-badge" />
<img src="https://img.shields.io/badge/Players-2-red?style=for-the-badge" />

</div>

---

## 🎬 Project Overview

**1D LED PONG** is a fully interactive **hardware arcade game** built using an Arduino Uno and a WS2812B LED strip.

A single LED acts as a **“ball”**, bouncing between two players.  
Timing, reaction speed, and strategy determine who wins the rally.

> 🏓 First player to reach the winning score dominates the strip.

---

## ⚡ What Makes This Different?

This is NOT a simple LED animation project.

It is a:

- 🎮 Fully **state-machine-driven arcade engine**
- ⚡ Real-time **physics-based movement system**
- 🎯 Competitive **reaction + timing game**
- 🧠 Embedded software structured like a game engine

---

## 🔥 Core Gameplay Loop

```text
Serve → Ball Movement → Zone Entry → Player Reaction
     → Hit / Power Input → Speed Calculation → Return
     → Miss → Point Scored → Re-serve → Repeat
