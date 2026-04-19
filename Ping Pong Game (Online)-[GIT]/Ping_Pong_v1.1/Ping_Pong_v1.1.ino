/*
 * ============================================================
 *  1D LED Strip PONG - Arduino Uno + TM1637 Edition
 * ============================================================
 *  Original WS2812B 1D Pong by B.Stultiens (GPL v3)
 *  Adapted by: Hashtag
 *
 *  HARDWARE:
 *   - Arduino Uno
 *   - 60× WS2812B LED strip  (game field)
 *   - TM1637 4-digit display  (score)
 *   - Passive buzzer on Pin 9 (Timer1 OC1A, hardware toggle)
 *   - 4 push buttons (2 per player, INPUT_PULLUP / active-LOW)
 *
 *  WIRING:
 *   Pin  2  → WS2812B data
 *   Pin  3  → P2 Hit  button  (RIGHT start/return)
 *   Pin  4  → P2 Power button (RIGHT fast/boost)
 *   Pin  5  → P1 Hit  button  (LEFT  start/return)
 *   Pin  6  → P1 Power button (LEFT  fast/boost)
 *   Pin  9  → Buzzer + (minus to GND)
 *   Pin 11  → TM1637 CLK
 *   Pin 12  → TM1637 DIO
 *
 *  PLAYER COLORS (change COL_P1/P2 defines to remap):
 *   P1 (Left)  = RED
 *   P2 (Right) = PINK (Hot-pink / Deep-pink)
 *
 *  DISPLAY FORMAT:
 *   Idle  → ----
 *   Start → PLAY   (+ beep)
 *   Game  → 2--7   (P1 score -- P2 score)
 *   Win   → final score stays
 *
 *  WIN CONDITION: first to WIN_POINTS (9) points
 *  WIN ANIMATION: entire strip fades in/out with winner's colour
 *
 *  IDLE AUTO-RETURN: 30 s of inactivity on start screen → idle
 * ============================================================
 *
 *  LIBRARIES NEEDED (install via Arduino Library Manager):
 *   - Adafruit NeoPixel
 *   - TM1637Display (avishorp)
 * ============================================================
 */

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>
#include "notes.h"          // Pitch table & note/duration defines

// ── Utility ────────────────────────────────────────────────
#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// ── Pin assignments ─────────────────────────────────────────
#define PIN_WSDATA   2   // WS2812B data line
#define PIN_BUT_RS   4   // P2 Hit  (Right-Start)
#define PIN_BUT_RP   5   // P2 Power (Right-Power)
#define PIN_BUT_LS   6   // P1 Hit  (Left-Start)
#define PIN_BUT_LP   7   // P1 Power (Left-Power)
#define PIN_SOUND    8   // Buzzer  (Timer1 OC1A – Pin 9 on Uno)
#define PIN_TM_CLK  13   // TM1637 clock
#define PIN_TM_DIO  12   // TM1637 data

// ── Game parameters ─────────────────────────────────────────
#define NPIXELS          60   // Total LEDs in strip
#define ZONE_SIZE         7   // Default hit-zone size each side
#define SHOW_LO          12   // Score-dot dim brightness (0-255)
#define SHOW_HI          48   // Score-dot bright brightness
#define WIN_POINTS        9   // Points to win  (changed from 10 to 9)
#define TONE_INTERVAL     5   // Play move-tick sound every N steps

// ── Player colors  ──────────────────────────────────────────
// Change these to remap player strip colours.
// Each triplet is (R, G, B) 0-255.
#define COL_P1_R  255             // P1 = RED
#define COL_P1_G    0
#define COL_P1_B    0

#define COL_P2_R  255             // P2 = HOT PINK
#define COL_P2_G   20
#define COL_P2_B  147

// Dim versions used for zones and score dots
#define COL_ZONE_P1_R  (COL_P1_R/4)
#define COL_ZONE_P1_G  (COL_P1_G/4)
#define COL_ZONE_P1_B  (COL_P1_B/4)

#define COL_ZONE_P2_R  (COL_P2_R/4)
#define COL_ZONE_P2_G  (COL_P2_G/4)
#define COL_ZONE_P2_B  (COL_P2_B/4)

// ── TM1637 raw segment codes ────────────────────────────────
//  Segment bits:  .gfedcba
#define SEG_DASH   0x40   // Middle bar only  ( – )
#define SEG_P      0x73   // Letter P
#define SEG_L      0x38   // Letter L
#define SEG_A      0x77   // Letter A
#define SEG_Y      0x6E   // Letter Y
#define SEG_BLANK  0x00   // All off

// ── Event flags (bitmask) ───────────────────────────────────
#define EV_BUT_LS_PRESS  0x01   // P1 Hit button pressed
#define EV_BUT_RS_PRESS  0x02   // P2 Hit button pressed
#define EV_BUT_LP_PRESS  0x04   // P1 Power button pressed
#define EV_BUT_RP_PRESS  0x08   // P2 Power button pressed
#define EV_TIMER         0x10   // General timer fired
#define EV_TIMEOUT       0x20   // Idle / resume timeout fired
#define EV_TONETIMER     0x40   // Sound note duration expired

// ── Timing (milliseconds) ───────────────────────────────────
#define TIME_DEBOUNCE       8       // Button settle time
#define TIME_IDLE          40       // Idle animation tick
#define TIME_START_TIMEOUT 30000    // 30 s – no serve → go idle
#define TIME_RESUME_TIMEOUT 7500    // 7.5 s – auto-serve after point
#define TIME_BALL_BLINK    150      // Ball-blink period at serve pos
#define TIME_SPEED_MIN      10      // Fastest ball step (ms)
#define TIME_SPEED_INTERVAL  3      // Speed gain per LED position
#define TIME_POINT_BLINK   233      // Score-dot blink after a point
#define TIME_WIN_BLINK      50      // Win fade update interval
#define TIME_LOCKOUT       250      // Anti-spam (max 4 presses/s)

// Sound durations
#define TIME_TONE_SERVE    50
#define TIME_TONE_BOUNCE   50
#define TIME_TONE_MOVE     25
#define TIME_TONE_SCORE    50

// ── Game states ─────────────────────────────────────────────
enum {
  ST_IDLE = 0,   // No game – rainbow animation + '----'
  ST_START_L,    // Waiting for P1 serve
  ST_START_R,    // Waiting for P2 serve
  ST_MOVE_LR,    // Ball flying left→right (outside zones)
  ST_MOVE_RL,    // Ball flying right→left (outside zones)
  ST_ZONE_L,     // Ball inside P1's hit zone
  ST_ZONE_R,     // Ball inside P2's hit zone
  ST_POINT_L,    // P1 just scored – blink animation
  ST_POINT_R,    // P2 just scored – blink animation
  ST_RESUME_L,   // P1 scored; waiting for P1 to re-serve
  ST_RESUME_R,   // P2 scored; waiting for P2 to re-serve
  ST_WIN_L,      // P1 wins – full-strip red fade loop
  ST_WIN_R,      // P2 wins – full-strip pink fade loop
};

// ── Library objects ─────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, PIN_WSDATA, NEO_GRB | NEO_KHZ800);
TM1637Display     disp(PIN_TM_CLK, PIN_TM_DIO);

// ── Global state ────────────────────────────────────────────
static uint32_t oldtime;           // Previous millis() snapshot

static uint8_t  thestate;          // Current FSM state

// Button stable states and debounce countdown timers
static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

// Countdown timers
static uint16_t timer;             // General-purpose FSM timer
static uint16_t timeout;           // Idle / resume auto-fire timer
static uint16_t tonetimer;         // Active note duration
static uint16_t lockout_l;         // P1 anti-spam gate
static uint16_t lockout_r;         // P2 anti-spam gate

// Game variables
static uint8_t  ballblinkstate;    // Ball on/off at serve position
static uint8_t  pointblinkcount;   // Remaining blinks after a point
static uint8_t  ballpos;           // Current ball LED index (0 … NPIXELS-1)
static uint16_t speed;             // Ball step interval (ms)
static uint8_t  speedup;           // Cumulative rally-speedup counter
static uint8_t  points_l;         // P1 score
static uint8_t  points_r;         // P2 score
static uint8_t  zone_l, zone_r;   // Current zone sizes (can shrink with power)
static uint8_t  boost_l, boost_r; // How many times each player boosted this rally
static uint8_t  boosted;          // 1 if current ball is boosted (25% faster)
static uint8_t  tonecount;        // Countdown to next move-tick beep
static uint8_t  tuneidx;          // Index into win tune array

// Win-animation fade state
static int16_t  fade_val = 0;     // Current brightness (0-255)
static int8_t   fade_dir = 3;     // +3 = getting brighter, -3 = dimmer

// ── Tone pitch table (PROGMEM) ──────────────────────────────
// Timer1 OCR1A values for each note pitch at 16 MHz (CTC, no prescaler)
static const uint16_t tone_pitch[NTONE_PITCH] PROGMEM = {
  61155,57722,54482,51424,48538,45814,43242,40815,38524,36362,
  34321,32395,30577,28860,27240,25711,24268,22906,21620,20407,
  19261,18180,17160,16197,15288,14429,13619,12855,12133,11452,
  10809,10203, 9630, 9089, 8579, 8098, 7643, 7214, 6809, 6427,
   6066, 5725, 5404, 5101, 4814, 4544, 4289, 4048, 3821, 3606,
   3404, 3213, 3032, 2862, 2701, 2550, 2406, 2271, 2144, 2023,
   1910,
};

// ── Win tune (PROGMEM) ──────────────────────────────────────
typedef struct { uint8_t note; uint16_t duration; } note_t;

static const note_t tune_win[] PROGMEM = {
  { NOTE_Gs6, DUR_1_16 }, { NOTE_A6,  DUR_1_16 },
  { NOTE_Gs6, DUR_1_16 }, { NOTE_E6,  DUR_1_16 },
  { NOTE_Gs6, DUR_1_16 }, { NOTE_A6,  DUR_1_16 },
  { NOTE_Gs6, DUR_1_16 }, { NOTE_E6,  DUR_1_16 },
  { 0,        DUR_1_8  }, { NOTE_D4,  DUR_1_8  },
  { NOTE_D4,  DUR_1_8  }, { NOTE_B3,  DUR_1_8  },
  { NOTE_E4,  DUR_1_8  }, { NOTE_D4,  DUR_1_4  },
  { NOTE_B3,  DUR_1_4  }, { NOTE_D4,  DUR_1_8  },
  { NOTE_D4,  DUR_1_8  }, { NOTE_B3,  DUR_1_8  },
  { NOTE_E4,  DUR_1_8  }, { NOTE_D4,  DUR_1_4  },
  { NOTE_B3,  DUR_1_4  },
};

// ── Sound helpers ────────────────────────────────────────────

// Silence buzzer: set OC1A to clear-on-match (pin stays LOW)
#define sound_off()  do { TCCR1A = _BV(COM1A1); } while(0)

/*
 * set_tone()
 *  Programs Timer1 to toggle Pin 9 (OC1A) at the given pitch.
 *  note     = index into tone_pitch[] (1-based); 0 = silence
 *  duration = how long to play in ms (tracked by tonetimer)
 */
static inline void set_tone(uint16_t note, uint16_t duration) {
  tonetimer = duration;
  if (note && note <= NTONE_PITCH) {
    OCR1A  = pgm_read_word(&tone_pitch[note - 1]); // Load compare value
    TCCR1A = _BV(COM1A0);   // Toggle on match → square wave
    TCNT1  = 0;             // Reset counter so tone starts immediately
  } else {
    sound_off();            // note=0 → silence
  }
}

/*
 * tune_next()
 *  Advances to the next note in tune_win[].
 *  Called each time the current note's duration expires.
 */
static inline void tune_next() {
  if (tuneidx < NELEM(tune_win)) {
    uint16_t n = pgm_read_byte( &tune_win[tuneidx].note);
    uint16_t d = pgm_read_word(&tune_win[tuneidx].duration);
    set_tone(n, d);
    tuneidx++;
    Serial.print("TUNE: note idx="); Serial.println(tuneidx);
  } else {
    set_tone(0, 0);        // End of tune – go silent
    Serial.println("TUNE: finished");
  }
}

// ── TM1637 display helpers ───────────────────────────────────

/*
 * display_idle()
 *  Shows  ----  to indicate the game is not running.
 */
static void display_idle() {
  uint8_t seg[4] = { SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH };
  disp.setSegments(seg);
  Serial.println("DISPLAY: ----  (idle)");
}

/*
 * display_play()
 *  Shows  PLAY  when a player presses start from idle.
 *  TM1637 can show: P L A Y (all 4 digits used).
 */
static void display_play() {
  uint8_t seg[4] = { SEG_P, SEG_L, SEG_A, SEG_Y };
  disp.setSegments(seg);
  Serial.println("DISPLAY: PLAY");
}

/*
 * display_score()
 *  Shows current score as  P1--P2  e.g. "0--0" or "3--7".
 *  Digit layout: [P1 score] [dash] [dash] [P2 score]
 */
static void display_score() {
  uint8_t seg[4];
  seg[0] = disp.encodeDigit(points_l);  // Left  player score
  seg[1] = SEG_DASH;                    // separator
  seg[2] = SEG_DASH;                    // separator
  seg[3] = disp.encodeDigit(points_r);  // Right player score
  disp.setSegments(seg);
  Serial.print("DISPLAY: "); Serial.print(points_l);
  Serial.print("--");        Serial.println(points_r);
}

// ── Button helpers ───────────────────────────────────────────

/*
 * button_is_down()
 *  Returns non-zero if the button is currently pressed (held).
 *  Used to detect simultaneous Hit+Power for boost.
 */
static inline uint8_t button_is_down(uint8_t pin) {
  switch (pin) {
    case PIN_BUT_LS: return !debtmr_ls && !bstate_ls;
    case PIN_BUT_RS: return !debtmr_rs && !bstate_rs;
    case PIN_BUT_LP: return !debtmr_lp && !bstate_lp;
    case PIN_BUT_RP: return !debtmr_rp && !bstate_rp;
  }
  return 0;
}

/*
 * do_debounce()
 *  Tracks a button through a simple settle timer.
 *  Fires an event on the FALLING edge (press) only – INPUT_PULLUP logic.
 *  tdiff  = ms elapsed since last call
 *  bstate = stable state of the button (0=pressed, 1=released)
 *  debtmr = settle countdown (non-zero = ignore transitions)
 *  pin    = Arduino pin number
 *  ev     = event flag to return on a press
 */
static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
  if (0 == *debtmr) {
    uint8_t state = digitalRead(pin);
    if (state != *bstate) {         // State changed?
      *debtmr = TIME_DEBOUNCE;      // Start settle timer
      if (!(*bstate = state))       // Accept new state; Low = pressed
        return ev;                  // Return event on press only
    }
  } else {
    // Count down the settle timer
    if (*debtmr >= tdiff) *debtmr -= tdiff;
    else                  *debtmr = 0;
  }
  return 0;
}

/*
 * do_timer()
 *  Decrements *tmr by tdiff ms.
 *  Returns ev exactly once when the timer reaches zero.
 */
static inline uint8_t do_timer(uint8_t tdiff, uint16_t *tmr, uint8_t ev) {
  if (*tmr) {
    if (*tmr >= tdiff) *tmr -= tdiff;
    else               *tmr = 0;
    if (*tmr == 0) return ev;   // Fire event on expiry
  }
  return 0;
}

// ── Draw helpers ─────────────────────────────────────────────

/*
 * draw_sides()
 *  Lights the hit-back zones at each end of the strip.
 *  Left  (P1) zone = dim red
 *  Right (P2) zone = dim pink
 *  Zone size shrinks when a player uses the Power button.
 */
static void draw_sides() {
  // P1 zone – left end, dim red
  for (uint8_t i = 0; i < zone_l; i++) {
    one_d.setPixelColor(i, COL_ZONE_P1_R, COL_ZONE_P1_G, COL_ZONE_P1_B);
  }
  // P2 zone – right end, dim pink
  for (uint8_t i = 0; i < zone_r; i++) {
    one_d.setPixelColor(NPIXELS - 1 - i,
                        COL_ZONE_P2_R, COL_ZONE_P2_G, COL_ZONE_P2_B);
  }
}

/*
 * draw_ball()
 *  Draws the ball (yellow) at pos with a 5-LED diminishing tail.
 *  dir = +1 (moving right) or -1 (moving left) – tail goes opposite.
 */
static void draw_ball(int8_t dir, uint8_t pos) {
  uint8_t brightness = 255;
  int8_t  p = (int8_t)pos;
  for (uint8_t i = 0; i < 5; i++) {
    if (p >= 0 && p < NPIXELS)
      one_d.setPixelColor(p, brightness, brightness, 0);  // Yellow
    brightness >>= 1;   // Each tail pixel is half as bright
    p -= dir;           // Walk backward along direction of travel
  }
}

/*
 * draw_course()
 *  Clears the strip, redraws zones, and optionally draws score dots.
 *  v = 0      → zones only (used during fast ball-move animation)
 *  v = SHOW_LO/HI → also draw score dots at that brightness
 *  P1 score = red  dots expanding from centre-left
 *  P2 score = pink dots expanding from centre-right
 */
static void draw_course(uint8_t v) {
  one_d.clear();
  draw_sides();
  if (v) {
    // P1 score dots (red) – pairs grow outward from centre toward left
    for (uint8_t i = 0; i < points_l; i++) {
      one_d.setPixelColor(NPIXELS/2 - 1 - (2*i + 0), v, 0, 0);
      one_d.setPixelColor(NPIXELS/2 - 1 - (2*i + 1), v, 0, 0);
    }
    // P2 score dots (pink) – pairs grow outward from centre toward right
    for (uint8_t i = 0; i < points_r; i++) {
      one_d.setPixelColor(NPIXELS/2 + (2*i + 0), v, v/12, v/3);
      one_d.setPixelColor(NPIXELS/2 + (2*i + 1), v, v/12, v/3);
    }
  }
}

// ── Idle animation ───────────────────────────────────────────
static uint16_t ai_h;      // Current HSV hue position
static uint8_t  ai_state;  // Sub-step inside idle sequence
static uint8_t  ai_pos;    // Ball / fill position scratch variable

#define H_STEPS  1542      // Total hue steps in NeoPixel HSV space

static void animate_idle_init() {
  ai_h = 0; ai_state = 0; ai_pos = 0;
  Serial.println("IDLE: animation reset");
}

/*
 * animate_idle()
 *  Cycles through a sequence of visual demos (called every TIME_IDLE ms):
 *   States 0-3  : full-strip rainbow wave (RGB cycle)
 *   States 4,6  : demo ball rolling left→right
 *   States 5,7  : demo ball rolling right→left
 *   States 8,10 : score expand (red left / pink right)
 *   States 9,11 : score collapse back to centre
 *   then loops
 */
static void animate_idle() {
  switch (ai_state) {

    case 0: case 1: case 2: case 3:
      // ── Rainbow wave: hue shifts across all LEDs ──────────
      for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        //one_d.setPixelColorHsv(i, h, 255, 128);
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, 128));
      }
      ai_h += H_STEPS / 60;
      if (ai_h >= H_STEPS) {
        ai_h -= H_STEPS;
        ai_pos = 0;
        ai_state++;
      }
      break;

    case 4: case 6:
      // ── Demo ball: left → right ───────────────────────────
      draw_course(0);
      draw_ball(1, ai_pos++);
      if (ai_pos >= NPIXELS) ai_state++;
      break;

    case 5: case 7:
      // ── Demo ball: right → left ───────────────────────────
      draw_course(0);
      draw_ball(-1, --ai_pos);
      if (!ai_pos) ai_state++;
      break;

    case 8: case 10:
      // ── Score fill: expand outward from centre ────────────
      draw_course(0);
      for (uint8_t i = 0; i < ai_pos; i++) {
        one_d.setPixelColor(NPIXELS/2 - 1 - i, 255,  0,   0  ); // red
        one_d.setPixelColor(NPIXELS/2 + i,      255, 20, 147  ); // pink
      }
      if (++ai_pos >= NPIXELS/2) { ai_state++; ai_pos = 0; }
      break;

    case 9: case 11:
      // ── Score collapse: shrink back to centre ─────────────
      draw_course(0);
      for (uint8_t i = 0; i < NPIXELS/2 - ai_pos; i++) {
        one_d.setPixelColor(NPIXELS/2 - 1 - i, 255,  0,   0  );
        one_d.setPixelColor(NPIXELS/2 + i,      255, 20, 147  );
      }
      if (++ai_pos >= NPIXELS/2) { ai_state++; ai_pos = 0; }
      break;

    default:
      ai_state = 0;          // Loop back to rainbow
      break;
  }
  one_d.show();
}

// ── Win animation ────────────────────────────────────────────

static void animate_win_init() {
  fade_val = 0;
  fade_dir = 3;
  Serial.println("WIN: fade animation initialized");
}

/*
 * animate_win()
 *  Fades the ENTIRE strip in and out with the winner's colour.
 *  side = 0 → P1 wins → RED   fade
 *  side = 1 → P2 wins → PINK  fade
 *
 *  Returns 1 always – exits only when a button is pressed
 *  (which triggers set_state(ST_START_x) in the main state machine).
 *
 *  Called every TIME_WIN_BLINK ms by the EV_TIMER handler.
 */
static uint8_t animate_win(uint8_t side) {
  uint8_t bv = (uint8_t)fade_val;  // Current brightness 0-255

  for (uint8_t i = 0; i < NPIXELS; i++) {
    if (side) {
      // P2 wins → pink  (R=full, G=tiny, B=half)
      one_d.setPixelColor(i, bv, bv / 12, bv / 2);
    } else {
      // P1 wins → red   (R=full, G=0, B=0)
      one_d.setPixelColor(i, bv, 0, 0);
    }
  }
  one_d.show();

  // Advance fade: bounce between 0 and 255
  fade_val += fade_dir;
  if (fade_val >= 255) { fade_val = 255; fade_dir = -3; }
  if (fade_val <= 0)   { fade_val = 0;   fade_dir =  3; }

  return 1;  // Always keep going (loop until player presses button)
}

// ── State machine helpers ────────────────────────────────────

/*
 * is_game_state()
 *  Returns 1 for states where the lockout timer suppresses extra presses.
 *  During ball flight and zone time, we want to limit press spam.
 */
static uint8_t is_game_state(uint8_t s) {
  switch (s) {
    case ST_MOVE_LR: case ST_MOVE_RL:
    case ST_ZONE_L:  case ST_ZONE_R:
    case ST_POINT_L: case ST_POINT_R:
    case ST_WIN_L:   case ST_WIN_R:
      return 1;
    default:
      return 0;
  }
}

/*
 * speed_to_timer()
 *  Sets 'timer' to the ball-step interval.
 *  If the ball is boosted (power button was used), travel is 25% faster.
 *  Minimum clamped to 2 ms to avoid lockup.
 */
static inline void speed_to_timer() {
  timer = boosted ? speed * 3 / 4 : speed;
  if (timer < 2) timer = 2;
}

// ── State transition ─────────────────────────────────────────

/*
 * set_state()
 *  The single function that changes thestate.
 *  Runs EXIT actions for the old state, then ENTRY actions for the new one.
 *  All LED and display updates triggered by state changes happen here.
 */
static void set_state(uint8_t newstate) {

  Serial.print("STATE: ");
  Serial.print(thestate);
  Serial.print(" -> ");
  Serial.println(newstate);

  // ────────────────── EXIT ACTIONS ──────────────────────────
  switch (thestate) {

    case ST_IDLE:
    case ST_WIN_L:
    case ST_WIN_R:
      // Full reset when starting a fresh game
      points_l = points_r = 0;
      boost_l  = boost_r  = 0;
      zone_l   = zone_r   = ZONE_SIZE;
      speedup  = 0;
      boosted  = 0;
      Serial.println("EXIT: scores/zones reset");
      break;

    // P1 side is about to serve → ball starts at position 0
    case ST_START_L:
    case ST_POINT_L:
    case ST_RESUME_L:
      ballpos = 0;
      speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL; // moderate serve speed
      speedup = 0;
      break;

    // P2 side is about to serve → ball starts at position NPIXELS-1
    case ST_START_R:
    case ST_POINT_R:
    case ST_RESUME_R:
      ballpos = NPIXELS - 1;
      speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
      speedup = 0;
      break;

    case ST_ZONE_L:
      // Ball was near P1's wall; distance from wall determines return speed
      speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
      if (++speedup / 2 >= speed) speed = 2;   // Clamp to minimum
      else                        speed -= speedup / 2;
      boosted = 0;
      Serial.print("EXIT ZONE_L: new speed="); Serial.println(speed);
      break;

    case ST_ZONE_R:
      // Ball was near P2's wall
      speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS - 1 - ballpos);
      if (++speedup / 2 >= speed) speed = 2;
      else                        speed -= speedup / 2;
      boosted = 0;
      Serial.print("EXIT ZONE_R: new speed="); Serial.println(speed);
      break;
  }

  thestate = newstate;

  // ────────────────── ENTRY ACTIONS ─────────────────────────
  switch (thestate) {

    // ── IDLE ──────────────────────────────────────────────────
    case ST_IDLE:
      boost_l = boost_r = 0;
      zone_l  = zone_r  = ZONE_SIZE;
      animate_idle_init();
      timer = TIME_IDLE;
      display_idle();                        // Show '----' on TM1637
      Serial.println("ENTER: IDLE – RGB rainbow + '----'");
      break;

    // ── START (serve screen) ───────────────────────────────────
    case ST_START_L:
    case ST_START_R:
      draw_course(SHOW_HI);
      one_d.show();
      timer          = TIME_BALL_BLINK;
      timeout        = TIME_START_TIMEOUT;   // 30 s to idle if no serve
      ballblinkstate = 0;
      ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;

      display_play();                        // Show 'PLAY' on TM1637
      set_tone(NOTE_G5, 300);               // Single start beep (non-blocking)
      Serial.println("ENTER: START – showing PLAY, beep played");
      Serial.print  ("  ball at pos="); Serial.println(ballpos);
      break;

    // ── BALL IN FLIGHT ─────────────────────────────────────────
    case ST_MOVE_LR:
    case ST_MOVE_RL:
      speed_to_timer();
      tonecount = TONE_INTERVAL;            // Reset move-tick counter
      display_score();                      // Show live score on TM1637
      Serial.print("ENTER: MOVE – speed="); Serial.println(speed);
      break;

    // ── POINT SCORED ──────────────────────────────────────────
    case ST_POINT_L:
    case ST_POINT_R:
      pointblinkcount = 7;
      // Partially restore zones if player didn't boost last rally
      if (!boost_l && zone_l < ZONE_SIZE) zone_l++;
      if (!boost_r && zone_r < ZONE_SIZE) zone_r++;
      timer = TIME_POINT_BLINK;
      if (boost_l) boost_l--;              // Decay boost counter
      if (boost_r) boost_r--;
      lockout_l = lockout_r = TIME_LOCKOUT; // Force seeing the score blink
      display_score();                      // Update TM1637 immediately
      Serial.print("ENTER: POINT – P1="); Serial.print(points_l);
      Serial.print("  P2=");              Serial.println(points_r);
      break;

    // ── RESUME (wait for scorer to re-serve) ──────────────────
    case ST_RESUME_L:
    case ST_RESUME_R:
      draw_course(SHOW_HI);
      one_d.show();
      timer          = TIME_BALL_BLINK;
      timeout        = TIME_RESUME_TIMEOUT; // 7.5 s auto-serve
      ballblinkstate = 0;
      display_score();                      // Keep score visible
      Serial.println("ENTER: RESUME – waiting for serve");
      break;

    // ── WIN – P1 (left, RED) ───────────────────────────────────
    case ST_WIN_L:
      lockout_l = lockout_r = 2 * TIME_LOCKOUT;
      animate_win_init();
      timer   = TIME_WIN_BLINK;
      tuneidx = 0;
      tune_next();                          // Start the victory jingle
      display_score();                      // Show final score on TM1637
      Serial.println("WINNER: P1 (LEFT – RED)");
      Serial.print  ("  Final score – P1: "); Serial.print(points_l);
      Serial.print  ("  P2: ");               Serial.println(points_r);
      break;

    // ── WIN – P2 (right, PINK) ─────────────────────────────────
    case ST_WIN_R:
      lockout_l = lockout_r = 2 * TIME_LOCKOUT;
      animate_win_init();
      timer   = TIME_WIN_BLINK;
      tuneidx = 0;
      tune_next();
      display_score();                      // Show final score on TM1637
      Serial.println("WINNER: P2 (RIGHT – PINK)");
      Serial.print  ("  Final score – P1: "); Serial.print(points_l);
      Serial.print  ("  P2: ");               Serial.println(points_r);
      break;
  }
}

// ── Arduino setup ────────────────────────────────────────────

void setup() {
  Serial.begin(9600);
  Serial.println("==============================");
  Serial.println("  1D LED PONG  – STARTUP");
  Serial.println("==============================");
  Serial.print("WIN_POINTS : "); Serial.println(WIN_POINTS);
  Serial.print("NPIXELS    : "); Serial.println(NPIXELS);
  Serial.print("P1 color   : RED  ("); Serial.print(COL_P1_R);
  Serial.print(","); Serial.print(COL_P1_G);
  Serial.print(","); Serial.print(COL_P1_B); Serial.println(")");
  Serial.print("P2 color   : PINK ("); Serial.print(COL_P2_R);
  Serial.print(","); Serial.print(COL_P2_G);
  Serial.print(","); Serial.print(COL_P2_B); Serial.println(")");

  // Pull all unused pins high to prevent floating inputs causing noise
  PORTB = PORTC = PORTD = 0xff;

  // Button inputs – active LOW (pressed = GND through button)
  pinMode(PIN_BUT_LS, INPUT_PULLUP);
  pinMode(PIN_BUT_RS, INPUT_PULLUP);
  pinMode(PIN_BUT_LP, INPUT_PULLUP);
  pinMode(PIN_BUT_RP, INPUT_PULLUP);
  Serial.println("SETUP: Buttons → INPUT_PULLUP");

  // Buzzer output
  digitalWrite(PIN_SOUND, LOW);
  pinMode(PIN_SOUND, OUTPUT);
  Serial.println("SETUP: Buzzer on Pin 9");

  // TM1637 display
  disp.setBrightness(5);   // Brightness 0-7; 5 is comfortable indoors
  display_idle();           // Show '----' immediately on power-up
  Serial.println("SETUP: TM1637 ready");

  // WS2812B LED strip
  one_d.begin();
  one_d.show();            // All LEDs off on startup
  Serial.println("SETUP: LED strip ready");

  // Enter idle state (runs both exit and entry actions)
  thestate = ST_IDLE;
  set_state(ST_IDLE);

  // ── Timer 1 setup (hardware tone generation) ──────────────
  // CTC mode (WGM12), no prescaler (CS10) → resolution = 62.5 ns @ 16 MHz
  // OC1A = Pin 9; toggled by hardware on compare match → avoids ISR jitter
  // that would cause clicks when NeoPixel updates disable interrupts.
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1A  = NOTE_C4;   // Arbitrary initial value
  TCNT1  = 0;
  Serial.println("SETUP: Timer1 configured for hardware tone");
  Serial.println("Press any Hit button to start!");
}

// ── Main loop ────────────────────────────────────────────────

#define chk_ev(ev)  (events & (ev))   // Test an event bit

void loop() {
  uint32_t now;
  // tdiff = elapsed ms since last loop iteration (usually 0 or 1)
  uint8_t  tdiff  = (now = millis()) - oldtime;
  uint8_t  events = 0;

  // ── Process inputs and timers every ms ──────────────────────
  if (tdiff) {
    oldtime = now;

    // Debounce all 4 buttons; accumulate press events
    events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, PIN_BUT_LS, EV_BUT_LS_PRESS);
    events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, PIN_BUT_RS, EV_BUT_RS_PRESS);
    events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, PIN_BUT_LP, EV_BUT_LP_PRESS);
    events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, PIN_BUT_RP, EV_BUT_RP_PRESS);

    // Countdown all timers; set their event bits when they hit zero
    events |= do_timer(tdiff, &timer,     EV_TIMER);
    events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
    events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);

    // Lockout timers: count down without firing events
    do_timer(tdiff, &lockout_l, 0);
    do_timer(tdiff, &lockout_r, 0);
  }

  // ── Lockout gate (suppress button events during active play) ─
  if (is_game_state(thestate)) {
    if (lockout_l) {
      events &= ~EV_BUT_LS_PRESS;   // Eat P1 press while locked
    }
    if (lockout_r) {
      events &= ~EV_BUT_RS_PRESS;   // Eat P2 press while locked
    }
  }

  // Any accepted press resets that player's lockout window
  if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
  if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;

  // ── State machine ────────────────────────────────────────────
  switch (thestate) {

    // ── IDLE: RGB rainbow plays; '----' on display ─────────────
    case ST_IDLE:
      if (chk_ev(EV_BUT_LS_PRESS)) {
        Serial.println("IDLE→START: P1 pressed");
        set_state(ST_START_L);
      } else if (chk_ev(EV_BUT_RS_PRESS)) {
        Serial.println("IDLE→START: P2 pressed");
        set_state(ST_START_R);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_IDLE;
        animate_idle();             // Advance animation one frame
      }
      break;

    // ── START_L: 'PLAY' on display; P1 ball blinks at left end ──
    case ST_START_L:
      if (chk_ev(EV_BUT_LS_PRESS)) {
        Serial.println("START_L→MOVE_LR: P1 served");
        set_state(ST_MOVE_LR);
      } else if (chk_ev(EV_TIMEOUT)) {
        Serial.println("START_L: 30 s timeout → IDLE");
        set_state(ST_IDLE);
      } else if (chk_ev(EV_TIMER)) {
        // Blink ball (warm-orange) to show serve position
        timer = TIME_BALL_BLINK;
        one_d.setPixelColor(ballpos,
          ballblinkstate ? 255 : 0,
          ballblinkstate ? 128 : 0,
          0);
        one_d.show();
        ballblinkstate = !ballblinkstate;
      }
      break;

    // ── START_R: 'PLAY' on display; P2 ball blinks at right end ─
    case ST_START_R:
      if (chk_ev(EV_BUT_RS_PRESS)) {
        Serial.println("START_R→MOVE_RL: P2 served");
        set_state(ST_MOVE_RL);
      } else if (chk_ev(EV_TIMEOUT)) {
        Serial.println("START_R: 30 s timeout → IDLE");
        set_state(ST_IDLE);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_BALL_BLINK;
        one_d.setPixelColor(ballpos,
          ballblinkstate ? 255 : 0,
          ballblinkstate ? 128 : 0,
          0);
        one_d.show();
        ballblinkstate = !ballblinkstate;
      }
      break;

    // ── MOVE_LR: Ball flying left → right (outside P2 zone) ─────
    case ST_MOVE_LR:
      if (chk_ev(EV_TIMER)) {
        // Play a soft tick every TONE_INTERVAL steps
        if (!--tonecount) {
          set_tone(NOTE_G4, TIME_TONE_MOVE);
          tonecount = TONE_INTERVAL;
        }
        speed_to_timer();
        draw_course(SHOW_LO);
        draw_ball(1, ballpos);
        one_d.show();
        ballpos++;
        // Enter P2's zone when close enough to the right wall
        if (NPIXELS - 1 - ballpos <= zone_r) {
          Serial.print("MOVE_LR: entering P2 zone at pos="); Serial.println(ballpos);
          set_state(ST_ZONE_R);
        }
      }
      break;

    // ── MOVE_RL: Ball flying right → left (outside P1 zone) ─────
    case ST_MOVE_RL:
      if (chk_ev(EV_TIMER)) {
        if (!--tonecount) {
          set_tone(NOTE_G4, TIME_TONE_MOVE);
          tonecount = TONE_INTERVAL;
        }
        speed_to_timer();
        draw_course(SHOW_LO);
        draw_ball(-1, ballpos);
        one_d.show();
        ballpos--;
        // Enter P1's zone when close enough to the left wall
        if (ballpos <= zone_l) {
          Serial.print("MOVE_RL: entering P1 zone at pos="); Serial.println(ballpos);
          set_state(ST_ZONE_L);
        }
      }
      break;

    // ── ZONE_L: Ball in P1's hit zone ────────────────────────────
    case ST_ZONE_L:
      if (chk_ev(EV_BUT_LS_PRESS)) {
        // P1 returns the ball
        Serial.print("ZONE_L: P1 hit at pos="); Serial.println(ballpos);
        set_tone(NOTE_G3, TIME_TONE_BOUNCE);
        set_state(ST_MOVE_LR);
        // Power boost: also holding Power button AND zone has room to shrink
        if (zone_l > 1 && button_is_down(PIN_BUT_LP)) {
          zone_l--;      // Shrink P1's zone (harder to defend)
          boosted = 1;   // Ball travels 25% faster
          speed_to_timer();
          boost_l++;
          Serial.println("ZONE_L: P1 POWER BOOST activated!");
        }
      } else if (chk_ev(EV_TIMER)) {
        // No hit yet – ball keeps rolling toward P1's wall
        if (!ballpos) {
          // Ball reached left wall → P2 scores!
          Serial.println("ZONE_L: P1 MISSED – P2 scores!");
          set_tone(NOTE_C5, TIME_TONE_SCORE);
          if (++points_r >= WIN_POINTS) {
            Serial.println("GAME OVER: P2 WINS!");
            set_state(ST_WIN_R);
          } else {
            set_state(ST_POINT_R);
          }
        } else {
          speed_to_timer();
          ballpos--;
        }
        draw_course(SHOW_LO);
        draw_ball(-1, ballpos);
        one_d.show();
      }
      break;

    // ── ZONE_R: Ball in P2's hit zone ────────────────────────────
    case ST_ZONE_R:
      if (chk_ev(EV_BUT_RS_PRESS)) {
        // P2 returns the ball
        Serial.print("ZONE_R: P2 hit at pos="); Serial.println(ballpos);
        set_tone(NOTE_G3, TIME_TONE_BOUNCE);
        set_state(ST_MOVE_RL);
        // Power boost: also holding Power button
        if (zone_r > 1 && button_is_down(PIN_BUT_RP)) {
          zone_r--;
          boosted = 1;
          speed_to_timer();
          boost_r++;
          Serial.println("ZONE_R: P2 POWER BOOST activated!");
        }
      } else if (chk_ev(EV_TIMER)) {
        if (ballpos == NPIXELS - 1) {
          // Ball reached right wall → P1 scores!
          Serial.println("ZONE_R: P2 MISSED – P1 scores!");
          set_tone(NOTE_C5, TIME_TONE_SCORE);
          if (++points_l >= WIN_POINTS) {
            Serial.println("GAME OVER: P1 WINS!");
            set_state(ST_WIN_L);
          } else {
            set_state(ST_POINT_L);
          }
        } else {
          speed_to_timer();
          ballpos++;
        }
        draw_course(SHOW_LO);
        draw_ball(1, ballpos);
        one_d.show();
      }
      break;

    // ── POINT_L: P1 scored – blink the new score dot (red) ───────
    case ST_POINT_L:
      if (chk_ev(EV_BUT_LS_PRESS)) {
        // P1 can skip the blink animation
        set_state(ST_RESUME_L);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_POINT_BLINK;
        draw_course(SHOW_HI);
        // Blink only the newest P1 score dot pair (red ↔ off)
        uint8_t dot0 = NPIXELS/2 - 1 - (2*(points_l-1)+0);
        uint8_t dot1 = NPIXELS/2 - 1 - (2*(points_l-1)+1);
        if (pointblinkcount & 0x01) {
          one_d.setPixelColor(dot0, 255, 0, 0);  // Red ON
          one_d.setPixelColor(dot1, 255, 0, 0);
        } else {
          one_d.setPixelColor(dot0, 0, 0, 0);    // OFF
          one_d.setPixelColor(dot1, 0, 0, 0);
        }
        one_d.show();
        if (!--pointblinkcount) {
          Serial.println("POINT_L: blink done → RESUME_L");
          set_state(ST_RESUME_L);
        }
      }
      break;

    // ── POINT_R: P2 scored – blink the new score dot (pink) ──────
    case ST_POINT_R:
      if (chk_ev(EV_BUT_RS_PRESS)) {
        set_state(ST_RESUME_R);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_POINT_BLINK;
        draw_course(SHOW_HI);
        uint8_t dot0 = NPIXELS/2 + (2*(points_r-1)+0);
        uint8_t dot1 = NPIXELS/2 + (2*(points_r-1)+1);
        if (pointblinkcount & 0x01) {
          one_d.setPixelColor(dot0, 255, 20, 147); // Pink ON
          one_d.setPixelColor(dot1, 255, 20, 147);
        } else {
          one_d.setPixelColor(dot0, 0, 0, 0);      // OFF
          one_d.setPixelColor(dot1, 0, 0, 0);
        }
        one_d.show();
        if (!--pointblinkcount) {
          Serial.println("POINT_R: blink done → RESUME_R");
          set_state(ST_RESUME_R);
        }
      }
      break;

    // ── RESUME_L: P1 scored; waiting for P1 to re-serve ──────────
    case ST_RESUME_L:
      if (chk_ev(EV_BUT_LS_PRESS | EV_TIMEOUT)) {
        // P1 presses or auto-timeout launches the ball
        if (chk_ev(EV_TIMEOUT))
          Serial.println("RESUME_L: auto-timeout serve");
        else
          Serial.println("RESUME_L: P1 re-served");
        set_state(ST_MOVE_LR);
        set_tone(NOTE_F3, TIME_TONE_SERVE);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_BALL_BLINK;
        one_d.setPixelColor(ballpos,
          ballblinkstate ? 255 : 0,
          ballblinkstate ? 128 : 0,
          0);
        one_d.show();
        ballblinkstate = !ballblinkstate;
      }
      break;

    // ── RESUME_R: P2 scored; waiting for P2 to re-serve ──────────
    case ST_RESUME_R:
      if (chk_ev(EV_BUT_RS_PRESS | EV_TIMEOUT)) {
        if (chk_ev(EV_TIMEOUT))
          Serial.println("RESUME_R: auto-timeout serve");
        else
          Serial.println("RESUME_R: P2 re-served");
        set_state(ST_MOVE_RL);
        set_tone(NOTE_F3, TIME_TONE_SERVE);
      } else if (chk_ev(EV_TIMER)) {
        timer = TIME_BALL_BLINK;
        one_d.setPixelColor(ballpos,
          ballblinkstate ? 255 : 0,
          ballblinkstate ? 128 : 0,
          0);
        one_d.show();
        ballblinkstate = !ballblinkstate;
      }
      break;

    // ── WIN_L / WIN_R: Fade animation + victory jingle ───────────
    case ST_WIN_L:
    case ST_WIN_R:
      // Advance win jingle when current note expires
      if (chk_ev(EV_TONETIMER)) {
        events &= ~EV_TONETIMER;  // Consume so it doesn't re-trigger below
        tune_next();
      }
      // Either player pressing starts a new game
      if (chk_ev(EV_BUT_LS_PRESS)) {
        Serial.println("WIN: P1 pressed – new game, P1 serves");
        set_state(ST_START_L);
      } else if (chk_ev(EV_BUT_RS_PRESS)) {
        Serial.println("WIN: P2 pressed – new game, P2 serves");
        set_state(ST_START_R);
      } else if (chk_ev(EV_TIMER)) {
        // Advance the full-strip fade animation each tick
        timer = TIME_WIN_BLINK;
        animate_win(thestate == ST_WIN_R);  // Loops forever – returns 1 always
      }
      break;

    // ── FALLBACK: Should never happen ─────────────────────────────
    default:
      Serial.println("ERROR: Unknown state – resetting");
      set_state(ST_IDLE);
      break;
  }

  // ── Global tone-off (async to state machine) ────────────────
  // When any note's duration expires, silence the buzzer.
  // Handled here so every state benefits without code in each case.
  if (chk_ev(EV_TONETIMER))
    set_tone(0, 0);
}

// vim: syn=cpp
