/*
 * ============================================================
 *  1D LED PONG  v1.4  –  Arduino Uno
 * ============================================================
 *  Based on 1D Pong v1.3 (B.Stultiens, GPL v3)
 *
 *  CHANGES IN v2.0:
 *   1. Dual TM1637 displays
 *        disp1 (CLK=8, DIO=9)   → P1's score / status (left side)
 *        disp2 (CLK=10, DIO=11) → P2's score / status (right side)
 *        Scores shown right-justified: "  02", "  09", etc.
 *        Win screen   → Winner: "WOn " / Loser: "dEAd"
 *        Forfeit      → Forfeiter: "KnS " / Opponent: "WOn "
 *        Idle         → Both displays show "----"
 *   2. Forfeit mechanic
 *        Any player holding Hit + Power simultaneously for
 *        TIME_FORFEIT ms (10 s) forfeits.  Serial prints a
 *        countdown every second so you can see the hold timer.
 *   3. Dynamic LED score bar
 *        LEDS_PER_PT = (NPIXELS/2 - ZONE_SIZE) / WIN_POINTS
 *        Automatically scales when you change NPIXELS or WIN_POINTS.
 *   4. Updated pin layout
 *        Buzzer → pin 3 (was 8)
 *        TM1637 displays → pins 8-11 (was 12-13 single display)
 *
 *  LIBRARIES (install via Library Manager):
 *    Adafruit NeoPixel
 *    TM1637Display  (by avishorp)
 *
 *  WIRING:
 *    Pin  2  → WS2812B DIN  (330Ω series resistor recommended)
 *    Pin  3  → Passive buzzer + lead  (– to GND)
 *    Pin  4  → P1 Hit   button → GND   (INPUT_PULLUP, active LOW)
 *    Pin  5  → P1 Power button → GND
 *    Pin  6  → P2 Hit   button → GND
 *    Pin  7  → P2 Power button → GND
 *    Pin  8  → TM1637 #1 CLK  (P1 display)
 *    Pin  9  → TM1637 #1 DIO  (P1 display)
 *    Pin 10  → TM1637 #2 CLK  (P2 display)
 *    Pin 11  → TM1637 #2 DIO  (P2 display)
 *    5V / GND → strip, both displays, buzzer common rail
 * ============================================================
 */

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>

// Handy element-count macro
#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// ── Pin assignments ──────────────────────────────────────────
#define LED_PIN   2     // WS2812B data line
#define BUZZER    3     // Passive buzzer – tone() on any digital pin
#define P1_HIT    4     // P1 Hit   button (active LOW, INPUT_PULLUP)
#define P1_POWER  5     // P1 Power button (active LOW, INPUT_PULLUP)
#define P2_HIT    6     // P2 Hit   button (active LOW, INPUT_PULLUP)
#define P2_POWER  7     // P2 Power button (active LOW, INPUT_PULLUP)
#define TM1_CLK   8     // TM1637 display #1 clock  (P1, left)
#define TM1_DIO   9     // TM1637 display #1 data   (P1, left)
#define TM2_CLK  10     // TM1637 display #2 clock  (P2, right)
#define TM2_DIO  11     // TM1637 display #2 data   (P2, right)

// Aliases used inside legacy FSM logic so the names match pin labels
#define PIN_WSDATA   LED_PIN
#define PIN_SOUND    BUZZER
#define PIN_BUT_LS   P1_HIT
#define PIN_BUT_LP   P1_POWER
#define PIN_BUT_RS   P2_HIT
#define PIN_BUT_RP   P2_POWER

// ── Game parameters ─────────────────────────────────────────
#define NPIXELS          60   // Total WS2812B LEDs in the strip
#define ZONE_SIZE         7   // Default hit-zone width in LEDs per side
#define SHOW_LO          12   // Score-dot brightness while ball is in flight
#define SHOW_HI          48   // Score-dot brightness at rest / serve position
#define WIN_POINTS        9   // First player to reach this score wins
#define TONE_INTERVAL     5   // Fire a move-tick sound every N ball steps

// ── Dynamic score bar ────────────────────────────────────────
//
//  The score bar expands from the centre of the strip outward.
//  Each player gets  SCORE_PIXELS  LEDs to use for score dots,
//  and each point lights up  LEDS_PER_PT  of those LEDs.
//
//  Example: 60 LEDs, ZONE_SIZE=7, WIN_POINTS=9
//    SCORE_PIXELS = 30 - 7 = 23 LEDs per side
//    LEDS_PER_PT  = 23 / 9 = 2  LEDs per point
//
//  Change NPIXELS or WIN_POINTS and the bar adapts automatically.
#define SCORE_PIXELS  (NPIXELS / 2 - ZONE_SIZE)
#define LEDS_PER_PT   (SCORE_PIXELS / WIN_POINTS)

// ── Event flags (bit-mask returned by do_debounce / do_timer) ─
#define EV_BUT_LS_PRESS  0x01   // P1 Hit pressed   (falling edge)
#define EV_BUT_RS_PRESS  0x02   // P2 Hit pressed
#define EV_BUT_LP_PRESS  0x04   // P1 Power pressed
#define EV_BUT_RP_PRESS  0x08   // P2 Power pressed
#define EV_TIMER         0x10   // General FSM timer expired
#define EV_TIMEOUT       0x20   // Idle / start-screen timeout expired
#define EV_TONETIMER     0x40   // Current buzzer note duration expired

// ── Timing constants (all in milliseconds) ───────────────────
#define TIME_DEBOUNCE        8       // Button debounce settle window
#define TIME_IDLE           40       // Idle animation tick (~25 fps)
#define TIME_START_TIMEOUT  20000    // Auto-return to idle if serve never comes
#define TIME_BALL_BLINK     150      // Blink half-period for ball at serve pos
#define TIME_SPEED_MIN       10      // Absolute fastest ball step (ms)
#define TIME_SPEED_INTERVAL   3      // Speed gain per LED distance from wall
#define TIME_POINT_BLINK    233      // Score-dot blink period after a point
#define TIME_WIN_BLINK       85      // Win animation frame period
#define TIME_LOCKOUT        250      // Anti-mash gate: ~4 accepted presses/s max
#define TIME_TONE_BOUNCE     50      // Sound durations
#define TIME_TONE_MOVE       25
#define TIME_TONE_SCORE      50
#define TIME_TONE_SERVE      50
#define TIME_FORFEIT      10000      // Hold Hit+Power this long to forfeit (10 s)

// ── 7-segment letter constants ───────────────────────────────
//
//  TM1637 is a 7-segment display, so only a limited alphabet is
//  possible.  Segment bit layout: bit 6=g (middle), 5=f, 4=e,
//  3=d, 2=c, 1=b, 0=a (a = top bar, g = middle bar).
//
//  Letters used for display messages:
//    "WOn " → W(≈U), O, n, blank     (WIN / FORFEIT opponent won)
//    "dEAd" → d, E, A, d             (LOSE)
//    "KnS " → K(≈H), n, S, blank     (KMS / forfeit – 7-seg limitation noted)
//    "----" → four dashes             (idle)
//
#define SEG_DASH  0x40   // Centre bar  ( – )
#define SEG_BLNK  0x00   // All segments off
#define SEG_A     0x77   // A
#define SEG_d     0x5E   // d  (lowercase)
#define SEG_E     0x79   // E
#define SEG_H     0x76   // H  (best 7-seg approximation for K)
#define SEG_n     0x54   // n  (lowercase; also best approx for N)
#define SEG_O     0x3F   // O
#define SEG_S     0x6D   // S
#define SEG_U     0x3E   // U  (best 7-seg approximation for W)

// ── Game states ─────────────────────────────────────────────
enum {
    ST_IDLE = 0,                  // No game – rainbow idle animation
    ST_START_L, ST_START_R,       // Waiting for initial serve (ball blinks)
    ST_MOVE_LR, ST_MOVE_RL,       // Ball in flight
    ST_ZONE_L,  ST_ZONE_R,        // Ball inside a player's hit-zone
    ST_POINT_L, ST_POINT_R,       // Point just scored – blink new dot
    ST_RESUME_L,ST_RESUME_R,      // Scorer must re-serve (button-only, no auto)
    ST_WIN_L,   ST_WIN_R,         // Game over by reaching WIN_POINTS
    ST_FORFEIT_L, ST_FORFEIT_R,   // Game over by forfeit (P1 / P2 gave up)
};

// ── Library objects ──────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, LED_PIN, NEO_GRB | NEO_KHZ800);
TM1637Display     disp1(TM1_CLK, TM1_DIO);   // P1's display (left side)
TM1637Display     disp2(TM2_CLK, TM2_DIO);   // P2's display (right side)

// ─────────────────────────────────────────────────────────────
//  SELF-CONTAINED SOUND SYSTEM
//
//  No external notes.h needed.  Arduino tone() is non-blocking –
//  it returns immediately and the hardware timer handles the pin.
//  tonetimer tracks the note's remaining duration; EV_TONETIMER
//  fires in loop() to either silence it or advance the win jingle.
// ─────────────────────────────────────────────────────────────
#define SND_BOUNCE  196   // G3  – ball returned / hit
#define SND_TICK    392   // G4  – ball in-flight tick
#define SND_SCORE   523   // C5  – point scored
#define SND_SERVE   174   // F3  – serve launched

// Win jingle: { frequency Hz, duration ms } pairs stored in flash
typedef struct { uint16_t freq; uint16_t dur; } note_t;
static const note_t tune_win[] PROGMEM = {
    {1661,125},{1760,125},{1661,125},{1319,125},   // Gs6 A6 Gs6 E6
    {1661,125},{1760,125},{1661,125},{1319,125},   // repeat
    {   0,250},                                    // rest
    { 294,250},{ 294,250},{ 247,250},{ 330,250},  // D4 D4 B3 E4
    { 294,500},{ 247,500},                         // D4 B3 (longer)
    { 294,250},{ 294,250},{ 247,250},{ 330,250},  // repeat
    { 294,500},{ 247,500},
};

// ─────────────────────────────────────────────────────────────
//  GLOBAL GAME STATE
// ─────────────────────────────────────────────────────────────
static uint32_t oldtime;
static uint8_t  thestate;

// Debounced button states: 1 = released, 0 = pressed (INPUT_PULLUP)
static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
// Debounce countdown timers (counts down to 0, then stable read is accepted)
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

static uint16_t timer;        // General-purpose FSM countdown
static uint16_t timeout;      // Start-screen idle timeout countdown
static uint16_t tonetimer;    // Remaining duration of current buzzer note
static uint16_t lockout_l;    // P1 anti-mash countdown
static uint16_t lockout_r;    // P2 anti-mash countdown

// Forfeit hold timers – accumulate ms while both buttons of that player are held.
// When either reaches TIME_FORFEIT the player forfeits.
static uint16_t forfeit_hold_l;   // P1: accumulated hold time in ms
static uint16_t forfeit_hold_r;   // P2: accumulated hold time in ms

static uint8_t  ballblinkstate;    // Ball blink toggle (on/off)
static uint8_t  pointblinkcount;   // Remaining blink cycles after a point
static uint8_t  ballpos;           // Current ball LED index
static uint16_t speed;             // ms per ball step (lower = faster)
static uint8_t  speedup;           // Cumulative rally speed bonus
static uint8_t  points_l;          // P1 score
static uint8_t  points_r;          // P2 score
static uint8_t  zone_l, zone_r;    // Current hit-zone sizes (shrink on boost)
static uint8_t  boost_l, boost_r;  // How many boosts each used this rally
static uint8_t  boosted;           // Set while ball is travelling at boost speed
static uint8_t  tonecount;         // Move-tick countdown (fires every N steps)
static uint8_t  tuneidx;           // Current position in win jingle array
static uint8_t  aw_state;          // Win animation frame counter

// ── Idle animation state ─────────────────────────────────────
//  0       Fade-in  rainbow brightness  0 → 128  (~1.3 s)
//  1,2,3   Full     rainbow at brightness 128 (3 hue cycles, ~7.5 s)
//  4,6     Demo ball rolling L → R
//  5,7     Demo ball rolling R → L
//  8,10    Score fill expanding outward from centre
//  9,11    Score fill collapsing inward
//  12      Fade-out rainbow brightness 128 → 0  then loops to 0
static uint16_t ai_h;          // Current hue position
static uint8_t  ai_state;      // Idle sub-state
static uint8_t  ai_pos;        // Scratch position for ball / fill
static uint8_t  ai_brightness; // Rainbow brightness for fade in/out

#define H_STEPS  1542   // One full hue revolution in our hue unit space

// ─────────────────────────────────────────────────────────────
//  SOUND FUNCTIONS
// ─────────────────────────────────────────────────────────────

/*
 * set_tone(freq, duration)
 *  Start playing freq Hz on the buzzer for duration ms.
 *  freq = 0 silences immediately.  Returns at once (non-blocking).
 */
static inline void set_tone(uint16_t freq, uint16_t duration) {
    tonetimer = duration;
    if (freq) tone(PIN_SOUND, freq);
    else      noTone(PIN_SOUND);
}

/*
 * tune_next()
 *  Advance to and play the next note of the win jingle.
 *  Called each time EV_TONETIMER fires while in a win/forfeit state.
 */
static inline void tune_next() {
    if (tuneidx < NELEM(tune_win)) {
        uint16_t f = pgm_read_word(&tune_win[tuneidx].freq);
        uint16_t d = pgm_read_word(&tune_win[tuneidx].dur);
        set_tone(f, d);
        tuneidx++;
        Serial.print(F("[TUNE] note "));  Serial.print(tuneidx);
        Serial.print(F("/"));             Serial.print((uint8_t)NELEM(tune_win));
        Serial.print(F("  freq="));       Serial.println(f);
    } else {
        set_tone(0, 0);
        Serial.println(F("[TUNE] jingle finished"));
    }
}

// ─────────────────────────────────────────────────────────────
//  DISPLAY HELPERS  (dual TM1637)
//
//  Each helper accepts a TM1637Display reference so the same
//  function works for either disp1 (P1) or disp2 (P2).
// ─────────────────────────────────────────────────────────────

// Show  ----  on a single display (idle mode, no game running)
static void disp_dashes(TM1637Display &d) {
    uint8_t seg[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
    d.setSegments(seg, 4, 0);
}

// Show  ----  on BOTH displays simultaneously
static void disp_idle_both() {
    disp_dashes(disp1);
    disp_dashes(disp2);
}

/*
 * disp_score_on(d, score)
 *  Show player score right-justified as a 2-digit number.
 *  Format: "  0X"  (e.g. score 2 → "  02",  score 8 → "  08")
 *  Positions 0-1 are blank; positions 2-3 are the score digits.
 */
static void disp_score_on(TM1637Display &d, uint8_t score) {
    uint8_t seg[4] = {
        SEG_BLNK,
        SEG_BLNK,
        d.encodeDigit(score / 10),  // tens digit (0 for scores 0-9)
        d.encodeDigit(score % 10)   // units digit
    };
    d.setSegments(seg, 4, 0);
}

// Refresh both displays with the current game scores
static void disp_scores() {
    disp_score_on(disp1, points_l);   // P1 score on left  display
    disp_score_on(disp2, points_r);   // P2 score on right display
}

/*
 * disp_win_msg(d)   → "WOn "
 *  Shown on the winning player's display.
 *  W is approximated as U (best 7-seg can represent).
 */
static void disp_win_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_U, SEG_O, SEG_n, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

/*
 * disp_dead_msg(d)  → "dEAd"
 *  Shown on the losing player's display.
 *  All four letters render cleanly on 7-segment.
 */
static void disp_dead_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_d, SEG_E, SEG_A, SEG_d};
    d.setSegments(seg, 4, 0);
}

/*
 * disp_kms_msg(d)   → "KnS "
 *  Shown on the forfeiting player's display.
 *  K is approximated as H (true K impossible on 7-seg).
 *  M is approximated as n (true M impossible on 7-seg).
 *  Reads as "KMS" in context.
 */
static void disp_kms_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_H, SEG_n, SEG_S, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

// ─────────────────────────────────────────────────────────────
//  BUTTON HELPERS
// ─────────────────────────────────────────────────────────────

/*
 * button_is_down(pin)
 *  Returns true if the button is currently held (debounce settled).
 *  Used to detect simultaneous Hit+Power for the speed boost and
 *  for the forfeit hold detection.
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
 *  Filters contact bounce.  Fires an event ONLY on the falling edge
 *  (HIGH→LOW transition) which represents a button press.
 *  INPUT_PULLUP: HIGH = released, LOW = pressed.
 */
static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
    if (!*debtmr) {
        uint8_t state = digitalRead(pin);
        if (state != *bstate) {
            *debtmr = TIME_DEBOUNCE;           // start settle window
            if (!(*bstate = state)) return ev; // HIGH→LOW = press
        }
    } else {
        if (*debtmr >= tdiff) *debtmr -= tdiff;
        else                  *debtmr = 0;
    }
    return 0;
}

/*
 * do_timer()
 *  Counts a uint16_t countdown down to zero; fires ev exactly once.
 */
static inline uint8_t do_timer(uint8_t tdiff, uint16_t *tmr, uint8_t ev) {
    if (*tmr) {
        if (*tmr >= tdiff) *tmr -= tdiff;
        else               *tmr = 0;
        if (!*tmr) return ev;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  LED DRAW HELPERS
// ─────────────────────────────────────────────────────────────

/*
 * draw_sides()
 *  Lights the hit-zones at both ends of the strip in teal.
 *  Zone width can shrink when a player activates the Power boost.
 */
static void draw_sides() {
    // P1 zone: pixels 0 … zone_l-1  (left end, teal)
    for (uint8_t i = 0; i < zone_l; i++)
        one_d.setPixelColor(i, 0, 64, 64);
    // P2 zone: pixels NPIXELS-zone_r … NPIXELS-1  (right end, teal)
    for (uint8_t i = 0; i < zone_r; i++)
        one_d.setPixelColor(NPIXELS - 1 - i, 0, 64, 64);
}

/*
 * draw_ball(dir, pos)
 *  Draws a yellow ball at pos with a 5-pixel diminishing tail.
 *  dir = +1 (L→R): tail goes left.   dir = -1 (R→L): tail goes right.
 *  Uses uint8_t underflow (wraps to 255) to exit the < NPIXELS guard.
 */
static void draw_ball(int8_t dir, uint8_t pos) {
    uint8_t c = 255;
    for (uint8_t i = 0; i < 5 && pos < NPIXELS; i++) {
        one_d.setPixelColor(pos, c, c, 0);   // yellow
        c >>= 1;                              // dim each step
        pos -= dir;                           // tail is behind the ball
    }
}

/*
 * draw_course(v)
 *  Clear strip → draw hit-zones → draw score bars.
 *
 *  Score bar (dynamic, scales with NPIXELS and WIN_POINTS):
 *    P1: red  dots expand leftward  from centre (NPIXELS/2 - 1)
 *    P2: green dots expand rightward from centre (NPIXELS/2)
 *    Each point occupies LEDS_PER_PT LEDs.
 *
 *  v = SHOW_LO during ball flight (dim)
 *  v = SHOW_HI at rest / serve    (bright)
 *  v = 0 to skip score dots entirely (used in idle animation)
 */
static void draw_course(uint8_t v) {
    one_d.clear();
    draw_sides();

    if (v) {
        // P1 score: red dots grow leftward from the centre
        for (uint8_t i = 0; i < points_l; i++) {
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 - 1 - (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, v, 0, 0);
            }
        }
        // P2 score: green dots grow rightward from the centre
        for (uint8_t i = 0; i < points_r; i++) {
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 + (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, 0, v, 0);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  IDLE ANIMATION
// ─────────────────────────────────────────────────────────────

static void animate_idle_init() {
    ai_h = 0; ai_state = 0; ai_pos = 0; ai_brightness = 0;
    Serial.println(F("[IDLE] animation reset → fade-in starting"));
}

// Render the full rainbow at given brightness; does NOT advance ai_h.
static void draw_rainbow(uint8_t brightness) {
    for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        // H_STEPS (1542) → ColorHSV range (65535); ratio ≈ 42
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, brightness));
    }
}

/*
 * animate_idle()
 *  Called every TIME_IDLE ms (40 ms ≈ 25 fps) while in ST_IDLE.
 *  State machine with fade-in, full rainbow, demo ball, score fill,
 *  and fade-out phases that loop indefinitely.
 */
static void animate_idle() {
    switch (ai_state) {

        // ── State 0: rainbow FADE-IN (0 → 128 brightness) ────
        case 0:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness < 124) {
                ai_brightness += 4;
            } else {
                ai_brightness = 128;
                ai_state = 1;
                Serial.println(F("[IDLE] fade-in complete → full rainbow"));
            }
            break;

        // ── States 1-3: full rainbow, 3 complete hue cycles ──
        case 1: case 2: case 3:
            draw_rainbow(128);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) {
                ai_h -= H_STEPS;
                ai_pos = 0;
                ai_state++;   // one more cycle done; at 4 we switch to ball
            }
            break;

        // ── States 4,6: demo ball rolling L → R ──────────────
        case 4: case 6:
            draw_course(0);
            draw_ball(+1, ai_pos++);
            if (ai_pos >= NPIXELS) ai_state++;
            break;

        // ── States 5,7: demo ball rolling R → L ──────────────
        case 5: case 7:
            draw_course(0);
            draw_ball(-1, --ai_pos);
            if (!ai_pos) ai_state++;
            break;

        // ── States 8,10: score fill expanding outward ─────────
        case 8: case 10:
            draw_course(0);
            for (uint8_t i = 0; i < ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);   // red left
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);   // green right
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;

        // ── States 9,11: score fill collapsing inward ─────────
        case 9: case 11:
            draw_course(0);
            for (uint8_t i = 0; i < NPIXELS / 2 - ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;

        // ── State 12: rainbow FADE-OUT (128 → 0) then loop ───
        case 12:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness > 4) {
                ai_brightness -= 4;
            } else {
                ai_brightness = 0;
                ai_state = 0;   // loop: back to fade-in
                Serial.println(F("[IDLE] fade-out complete → looping"));
            }
            break;

        default:
            ai_state = 0; ai_brightness = 0;
            break;
    }
    one_d.show();
}

// ─────────────────────────────────────────────────────────────
//  WIN / FORFEIT ANIMATION
// ─────────────────────────────────────────────────────────────

static void animate_win_init() { aw_state = 0; }

/*
 * animate_win(side)
 *  Multi-phase wipe animation over the winner's half of the strip.
 *  side = 0 → P1 wins (left half, red)
 *  side = 1 → P2 wins (right half, green)
 *  Returns 1 while still running, 0 when complete (triggers ST_IDLE).
 */
static uint8_t animate_win(uint8_t side) {
    uint32_t clr;
    uint8_t  pos;
    if (side) { clr = Adafruit_NeoPixel::Color(0, 255, 0); pos = NPIXELS / 2; }
    else      { clr = Adafruit_NeoPixel::Color(255, 0, 0); pos = 0; }
    one_d.clear();
    if      (aw_state < 20)  { if (aw_state & 0x01) for (uint8_t i=0;i<NPIXELS/2;i++) one_d.setPixelColor(pos+i,          clr); }
    else if (aw_state < 50)  { for (uint8_t i=0;          i<aw_state-20;    i++) one_d.setPixelColor(pos+i,                clr); }
    else if (aw_state < 80)  { for (uint8_t i=aw_state-50;i<NPIXELS/2;     i++) one_d.setPixelColor(pos+i,                clr); }
    else if (aw_state < 110) { for (uint8_t i=0;          i<aw_state-80;    i++) one_d.setPixelColor(NPIXELS/2-1-i+pos,   clr); }
    else if (aw_state < 140) { for (uint8_t i=aw_state-110;i<NPIXELS/2;    i++) one_d.setPixelColor(NPIXELS/2-1-i+pos,   clr); }
    one_d.show();
    return ++aw_state < 140;
}

// ─────────────────────────────────────────────────────────────
//  STATE MACHINE HELPERS
// ─────────────────────────────────────────────────────────────

/*
 * is_game_state(s)
 *  Returns 1 for states where the lockout gate should suppress
 *  rapid button presses.  Prevents mashing during flight/score/end.
 */
static uint8_t is_game_state(uint8_t s) {
    switch (s) {
        case ST_MOVE_LR:   case ST_MOVE_RL:
        case ST_ZONE_L:    case ST_ZONE_R:
        case ST_POINT_L:   case ST_POINT_R:
        case ST_WIN_L:     case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            return 1;
        default:
            return 0;
    }
}

/*
 * speed_to_timer()
 *  Loads `timer` with the correct ball step interval.
 *  A boosted ball travels 25% faster.  Clamps to minimum 2 ms.
 */
static inline void speed_to_timer() {
    timer = boosted ? speed * 3 / 4 : speed;
    if (timer < 2) timer = 2;
}

// ─────────────────────────────────────────────────────────────
//  STATE TRANSITIONS
// ─────────────────────────────────────────────────────────────

/*
 * set_state(newstate)
 *  Runs EXIT actions for the old state, then ENTRY actions for
 *  the new state.  All LED / display / sound setup on a state
 *  change lives here so the FSM cases stay clean.
 */
static void set_state(uint8_t newstate) {
    Serial.print(F("[STATE] ")); Serial.print(thestate);
    Serial.print(F(" -> "));    Serial.println(newstate);

    // ── EXIT ACTIONS ─────────────────────────────────────────
    switch (thestate) {

        // Full reset any time a new game begins (from idle, win, or forfeit)
        case ST_IDLE:
        case ST_WIN_L:     case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            points_l = points_r = 0;
            boost_l  = boost_r  = 0;
            zone_l   = zone_r   = ZONE_SIZE;
            speedup  = 0;
            boosted  = 0;
            Serial.println(F("[RESET] scores, zones and boosts reset for new game"));
            break;

        // P1 side serves → ball starts at pixel 0
        case ST_START_L:
        case ST_POINT_L:
        case ST_RESUME_L:
            ballpos = 0;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P2 side serves → ball starts at last pixel
        case ST_START_R:
        case ST_POINT_R:
        case ST_RESUME_R:
            ballpos = NPIXELS - 1;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P1 just hit the ball – new speed based on proximity to wall
        case ST_ZONE_L:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] new speed=")); Serial.print(speed); Serial.println(F("ms"));
            break;

        // P2 just hit the ball
        case ST_ZONE_R:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS - 1 - ballpos);
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] new speed=")); Serial.print(speed); Serial.println(F("ms"));
            break;
    }

    thestate = newstate;

    // ── ENTRY ACTIONS ─────────────────────────────────────────
    switch (thestate) {

        // ── IDLE ─────────────────────────────────────────────
        case ST_IDLE:
            boost_l = boost_r = 0;
            zone_l  = zone_r  = ZONE_SIZE;
            animate_idle_init();
            timer = TIME_IDLE;
            disp_idle_both();   // both displays show ----
            Serial.println(F("[IDLE] awaiting player – rainbow playing – displays: ----"));
            break;

        // ── START screens (initial serve, ball blinks) ────────
        case ST_START_L:
        case ST_START_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = TIME_START_TIMEOUT;  // 20 s then return to idle
            ballblinkstate = 0;
            ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;
            disp_scores();   // show 0  0 on the two displays
            Serial.print(F("[START] "));
            Serial.print(thestate == ST_START_L ? F("P1") : F("P2"));
            Serial.println(F(" should press Hit to serve  (20 s timeout → idle)"));
            break;

        // ── Ball in flight ────────────────────────────────────
        case ST_MOVE_LR:
        case ST_MOVE_RL:
            speed_to_timer();
            tonecount = TONE_INTERVAL;
            disp_scores();
            Serial.print(F("[BALL] launched  speed="));
            Serial.print(speed);
            Serial.print(F("ms  dir="));
            Serial.println(thestate == ST_MOVE_LR ? F("L→R") : F("R→L"));
            break;

        // ── Point scored ──────────────────────────────────────
        case ST_POINT_L:
        case ST_POINT_R:
            pointblinkcount = 7;
            // Recover one zone LED for a player who did NOT boost last rally
            if (!boost_l && zone_l < ZONE_SIZE) zone_l++;
            if (!boost_r && zone_r < ZONE_SIZE) zone_r++;
            timer     = TIME_POINT_BLINK;
            if (boost_l) boost_l--;
            if (boost_r) boost_r--;
            lockout_l = lockout_r = TIME_LOCKOUT;
            disp_scores();   // update both displays with new scores
            Serial.print(F("[SCORE] P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));      Serial.print(points_r);
            Serial.print(F("  (need "));   Serial.print(WIN_POINTS);
            Serial.println(F(" to win)"));
            break;

        // ── RESUME – scorer re-serves; button-only, no auto-fire ──
        case ST_RESUME_L:
        case ST_RESUME_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = 0;           // disabled – must press to serve
            ballblinkstate = 0;
            disp_scores();
            Serial.print(F("[WAIT] "));
            Serial.print(thestate == ST_RESUME_L ? F("P1") : F("P2"));
            Serial.println(F(" press Hit to serve  (no auto-fire)"));
            break;

        // ── WIN: P1 scored WIN_POINTS ─────────────────────────
        case ST_WIN_L:
            disp_win_msg(disp1);    // P1 display → "WOn "
            disp_dead_msg(disp2);   // P2 display → "dEAd"
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer   = TIME_WIN_BLINK;
            tuneidx = 0;
            tune_next();
            Serial.println(F("[WIN] P1 wins!  disp1: 'WOn '  disp2: 'dEAd'"));
            Serial.print(F("[WIN] Final score  P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));                  Serial.println(points_r);
            Serial.println(F("[WIN] Press any Hit button to restart"));
            break;

        // ── WIN: P2 scored WIN_POINTS ─────────────────────────
        case ST_WIN_R:
            disp_dead_msg(disp1);   // P1 display → "dEAd"
            disp_win_msg(disp2);    // P2 display → "WOn "
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer   = TIME_WIN_BLINK;
            tuneidx = 0;
            tune_next();
            Serial.println(F("[WIN] P2 wins!  disp1: 'dEAd'  disp2: 'WOn '"));
            Serial.print(F("[WIN] Final score  P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));                  Serial.println(points_r);
            Serial.println(F("[WIN] Press any Hit button to restart"));
            break;

        // ── FORFEIT: P1 held Hit+Power for 10 s → P2 wins ────
        case ST_FORFEIT_L:
            disp_kms_msg(disp1);    // P1 display → "KnS " (KMS)
            disp_win_msg(disp2);    // P2 display → "WOn "
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            forfeit_hold_l = forfeit_hold_r = 0;   // reset so counters don't persist
            animate_win_init();
            timer   = TIME_WIN_BLINK;
            tuneidx = 0;
            tune_next();
            Serial.println(F("[FORFEIT] P1 forfeited!  P2 wins"));
            Serial.println(F("[FORFEIT] disp1: 'KnS '  disp2: 'WOn '"));
            Serial.println(F("[FORFEIT] Press any Hit button to restart"));
            break;

        // ── FORFEIT: P2 held Hit+Power for 10 s → P1 wins ────
        case ST_FORFEIT_R:
            disp_win_msg(disp1);    // P1 display → "WOn "
            disp_kms_msg(disp2);    // P2 display → "KnS " (KMS)
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            forfeit_hold_l = forfeit_hold_r = 0;
            animate_win_init();
            timer   = TIME_WIN_BLINK;
            tuneidx = 0;
            tune_next();
            Serial.println(F("[FORFEIT] P2 forfeited!  P1 wins"));
            Serial.println(F("[FORFEIT] disp1: 'WOn '  disp2: 'KnS '"));
            Serial.println(F("[FORFEIT] Press any Hit button to restart"));
            break;
    }
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    Serial.println(F("===================================="));
    Serial.println(F("  1D PONG  v2.0  –  STARTUP"));
    Serial.println(F("===================================="));
    Serial.print(F("  NPIXELS      = ")); Serial.println(NPIXELS);
    Serial.print(F("  WIN_POINTS   = ")); Serial.println(WIN_POINTS);
    Serial.print(F("  ZONE_SIZE    = ")); Serial.println(ZONE_SIZE);
    Serial.print(F("  SCORE_PIXELS = ")); Serial.print(SCORE_PIXELS);
    Serial.println(F(" LEDs per side available for score"));
    Serial.print(F("  LEDS_PER_PT  = ")); Serial.print(LEDS_PER_PT);
    Serial.println(F(" LEDs lit per score point"));
    Serial.print(F("  Total score bar: "));
    Serial.print(WIN_POINTS * LEDS_PER_PT * 2);
    Serial.println(F(" LEDs (both sides combined)"));
    Serial.print(F("  TIME_FORFEIT = ")); Serial.print(TIME_FORFEIT / 1000);
    Serial.println(F(" s (hold Hit+Power to forfeit)"));

    // Pull all unused port pins high to avoid floating inputs
    PORTB = PORTC = PORTD = 0xff;

    // Button inputs – pulled HIGH; pressing pulls to GND (active LOW)
    pinMode(P1_HIT,   INPUT_PULLUP);
    pinMode(P1_POWER, INPUT_PULLUP);
    pinMode(P2_HIT,   INPUT_PULLUP);
    pinMode(P2_POWER, INPUT_PULLUP);
    Serial.println(F("  Buttons  → INPUT_PULLUP (active LOW)"));

    // Buzzer output – start silent
    digitalWrite(BUZZER, LOW);
    pinMode(BUZZER, OUTPUT);
    Serial.println(F("  Buzzer   → Pin 3  (tone() non-blocking)"));

    // Both TM1637 displays at brightness 5 (range 0-7)
    disp1.setBrightness(5);
    disp2.setBrightness(5);
    disp_idle_both();   // show ---- on both immediately
    Serial.println(F("  disp1    → Pins 8(CLK)/9(DIO)   P1  brightness=5"));
    Serial.println(F("  disp2    → Pins 10(CLK)/11(DIO) P2  brightness=5"));

    // WS2812B strip
    one_d.begin();
    one_d.show();   // all LEDs off
    Serial.println(F("  LEDs     → Pin 2  (NEO_GRB + NEO_KHZ800)"));

    // Initialise FSM – triggers exit+entry actions for ST_IDLE
    thestate = ST_IDLE;
    set_state(ST_IDLE);

    Serial.println(F("===================================="));
    Serial.println(F("  Ready!  Press P1 or P2 Hit to start"));
    Serial.println(F("  During game: Hold Hit+Power 10 s to forfeit"));
    Serial.println(F("===================================="));
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────

#define chk_ev(ev)  (events & (ev))

void loop() {
    uint32_t now;
    uint8_t tdiff  = (now = millis()) - oldtime;
    uint8_t events = 0;

    // ── Tick everything once per ms ──────────────────────────
    if (tdiff) {
        oldtime = now;

        // Debounce all four buttons; events fire on falling edge only
        events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, P1_HIT,   EV_BUT_LS_PRESS);
        events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, P2_HIT,   EV_BUT_RS_PRESS);
        events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, P1_POWER, EV_BUT_LP_PRESS);
        events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, P2_POWER, EV_BUT_RP_PRESS);

        // Tick all countdown timers
        events |= do_timer(tdiff, &timer,     EV_TIMER);
        events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
        events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);
        do_timer(tdiff, &lockout_l, 0);   // lockout just counts down; no event needed
        do_timer(tdiff, &lockout_r, 0);
    }

    // ── Log every button edge to Serial ─────────────────────
    if (chk_ev(EV_BUT_LS_PRESS)) Serial.println(F("[BTN] P1 HIT pressed"));
    if (chk_ev(EV_BUT_RS_PRESS)) Serial.println(F("[BTN] P2 HIT pressed"));
    if (chk_ev(EV_BUT_LP_PRESS)) Serial.println(F("[BTN] P1 POWER pressed"));
    if (chk_ev(EV_BUT_RP_PRESS)) Serial.println(F("[BTN] P2 POWER pressed"));

    // ── Lockout gate: suppress button events in active game states ──
    // Prevents mashing from doing unintended things during flight / score.
    if (is_game_state(thestate)) {
        if (lockout_l) events &= ~EV_BUT_LS_PRESS;
        if (lockout_r) events &= ~EV_BUT_RS_PRESS;
    }
    // Any accepted Hit press re-arms that player's lockout window
    if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
    if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;

    // ── Forfeit detection ────────────────────────────────────
    //
    //  Active only while a game is live (not idle, not already in
    //  a win/forfeit state).  Each player's Hold+Power timer
    //  accumulates ms while both their buttons are held simultaneously.
    //  A Serial countdown (printed once per second) lets you track it.
    //  If either timer reaches TIME_FORFEIT (10 000 ms) → forfeit.
    //
    if (thestate != ST_IDLE &&
        thestate != ST_WIN_L    && thestate != ST_WIN_R &&
        thestate != ST_FORFEIT_L && thestate != ST_FORFEIT_R)
    {
        // ── P1 forfeit hold (Hit + Power simultaneously) ──────
        if (button_is_down(P1_HIT) && button_is_down(P1_POWER)) {
            uint16_t prev = forfeit_hold_l;
            forfeit_hold_l += tdiff;

            // Print once per second during the countdown
            if (forfeit_hold_l / 1000 != prev / 1000) {
                Serial.print(F("[FORFEIT] P1 holding: "));
                Serial.print(forfeit_hold_l / 1000);
                Serial.print(F(" / "));
                Serial.print(TIME_FORFEIT / 1000);
                Serial.println(F(" s..."));
            }

            if (forfeit_hold_l >= TIME_FORFEIT) {
                // P1 gives up → P2 wins
                set_state(ST_FORFEIT_L);
                // No return: state machine runs below with the new state
            }
        } else {
            // Buttons released – reset the P1 hold counter
            if (forfeit_hold_l > 0) {
                Serial.println(F("[FORFEIT] P1 released buttons – hold timer reset"));
                forfeit_hold_l = 0;
            }
        }

        // ── P2 forfeit hold (Hit + Power simultaneously) ──────
        if (button_is_down(P2_HIT) && button_is_down(P2_POWER)) {
            uint16_t prev = forfeit_hold_r;
            forfeit_hold_r += tdiff;

            if (forfeit_hold_r / 1000 != prev / 1000) {
                Serial.print(F("[FORFEIT] P2 holding: "));
                Serial.print(forfeit_hold_r / 1000);
                Serial.print(F(" / "));
                Serial.print(TIME_FORFEIT / 1000);
                Serial.println(F(" s..."));
            }

            if (forfeit_hold_r >= TIME_FORFEIT) {
                // P2 gives up → P1 wins
                set_state(ST_FORFEIT_R);
            }
        } else {
            if (forfeit_hold_r > 0) {
                Serial.println(F("[FORFEIT] P2 released buttons – hold timer reset"));
                forfeit_hold_r = 0;
            }
        }
    }

    // ── State machine ─────────────────────────────────────────
    switch (thestate) {

        // ── IDLE: rainbow plays; both displays show ---- ─────
        case ST_IDLE:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[IDLE] P1 starts game"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[IDLE] P2 starts game"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_IDLE;
                animate_idle();   // advance rainbow / demo sequence
            }
            break;

        // ── START_L: P1 must press Hit to serve ──────────────
        case ST_START_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[SERVE] P1 served"));
                set_state(ST_MOVE_LR);
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20 s timeout → idle"));
                set_state(ST_IDLE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                // Orange blink at ball position
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── START_R: P2 must press Hit to serve ──────────────
        case ST_START_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[SERVE] P2 served"));
                set_state(ST_MOVE_RL);
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20 s timeout → idle"));
                set_state(ST_IDLE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── MOVE_LR: ball flying left → right ────────────────
        case ST_MOVE_LR:
            if (chk_ev(EV_TIMER)) {
                // Periodic movement tick sound
                if (!--tonecount) {
                    set_tone(SND_TICK, TIME_TONE_MOVE);
                    tonecount = TONE_INTERVAL;
                }
                speed_to_timer();
                draw_course(SHOW_LO);
                draw_ball(+1, ballpos);
                one_d.show();
                ballpos++;
                // Enter P2's hit-zone when close enough to the right wall
                if (NPIXELS - 1 - ballpos <= zone_r) {
                    Serial.print(F("[BALL] entering P2 zone  pos=")); Serial.println(ballpos);
                    set_state(ST_ZONE_R);
                }
            }
            break;

        // ── MOVE_RL: ball flying right → left ────────────────
        case ST_MOVE_RL:
            if (chk_ev(EV_TIMER)) {
                if (!--tonecount) {
                    set_tone(SND_TICK, TIME_TONE_MOVE);
                    tonecount = TONE_INTERVAL;
                }
                speed_to_timer();
                draw_course(SHOW_LO);
                draw_ball(-1, ballpos);
                one_d.show();
                ballpos--;
                if (ballpos <= zone_l) {
                    Serial.print(F("[BALL] entering P1 zone  pos=")); Serial.println(ballpos);
                    set_state(ST_ZONE_L);
                }
            }
            break;

        // ── ZONE_L: ball is in P1's hit zone ─────────────────
        case ST_ZONE_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                // P1 returned the ball
                Serial.print(F("[HIT] P1 returned at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_LR);
                // Power boost: if Power is also held, shrink P1's zone for extra speed
                if (zone_l > 1 && button_is_down(P1_POWER)) {
                    zone_l--;
                    boosted = 1;
                    speed_to_timer();
                    boost_l++;
                    Serial.println(F("[BOOST] P1 power boost! Zone shrunk."));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (!ballpos) {
                    // Ball hit P1's wall → P2 scores a point
                    Serial.println(F("[MISS] P1 missed! P2 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_r >= WIN_POINTS) {
                        Serial.println(F("[WIN] P2 reached WIN_POINTS → wins!"));
                        set_state(ST_WIN_R);
                    } else {
                        set_state(ST_POINT_R);
                    }
                } else {
                    speed_to_timer();
                    ballpos--;   // keep moving toward the wall
                }
                draw_course(SHOW_LO);
                draw_ball(-1, ballpos);
                one_d.show();
            }
            break;

        // ── ZONE_R: ball is in P2's hit zone ─────────────────
        case ST_ZONE_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.print(F("[HIT] P2 returned at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_RL);
                if (zone_r > 1 && button_is_down(P2_POWER)) {
                    zone_r--;
                    boosted = 1;
                    speed_to_timer();
                    boost_r++;
                    Serial.println(F("[BOOST] P2 power boost! Zone shrunk."));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (ballpos == NPIXELS - 1) {
                    // Ball hit P2's wall → P1 scores a point
                    Serial.println(F("[MISS] P2 missed! P1 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_l >= WIN_POINTS) {
                        Serial.println(F("[WIN] P1 reached WIN_POINTS → wins!"));
                        set_state(ST_WIN_L);
                    } else {
                        set_state(ST_POINT_L);
                    }
                } else {
                    speed_to_timer();
                    ballpos++;
                }
                draw_course(SHOW_LO);
                draw_ball(+1, ballpos);
                one_d.show();
            }
            break;

        // ── POINT_L: P1 just scored – blink the new score LEDs ─
        case ST_POINT_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                // P1 can skip the blink and go straight to serving
                set_state(ST_RESUME_L);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                // Blink only the LEDs belonging to the most recent P1 point
                for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                    uint8_t px = NPIXELS / 2 - 1 - ((points_l - 1) * LEDS_PER_PT + j);
                    one_d.setPixelColor(px, (pointblinkcount & 0x01) ? SHOW_HI : 0, 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_L);
            }
            break;

        // ── POINT_R: P2 just scored – blink the new score LEDs ─
        case ST_POINT_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                set_state(ST_RESUME_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                    uint8_t px = NPIXELS / 2 + ((points_r - 1) * LEDS_PER_PT + j);
                    one_d.setPixelColor(px, 0, (pointblinkcount & 0x01) ? SHOW_HI : 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_R);
            }
            break;

        // ── RESUME_L: P1 scored; ONLY P1's Hit starts the next ball ─
        case ST_RESUME_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[SERVE] P1 re-serves"));
                set_state(ST_MOVE_LR);
                set_tone(SND_SERVE, TIME_TONE_SERVE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── RESUME_R: P2 scored; ONLY P2's Hit starts the next ball ─
        case ST_RESUME_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[SERVE] P2 re-serves"));
                set_state(ST_MOVE_RL);
                set_tone(SND_SERVE, TIME_TONE_SERVE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── WIN & FORFEIT: jingle plays + win animation runs ──
        //
        //  Both win and forfeit states share the same loop logic:
        //  advance the jingle on EV_TONETIMER, watch for a restart
        //  press, and drive the win animation on EV_TIMER.
        //
        //  Win/forfeit sides:
        //    ST_WIN_L    → P1 won  → animate side 0 (left, red)
        //    ST_WIN_R    → P2 won  → animate side 1 (right, green)
        //    ST_FORFEIT_L → P1 forfeited → P2 won → side 1 (green)
        //    ST_FORFEIT_R → P2 forfeited → P1 won → side 0 (red)
        //
        case ST_WIN_L: case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            if (chk_ev(EV_TONETIMER)) {
                events &= ~EV_TONETIMER;   // consume here; prevent global handler silencing it
                tune_next();               // advance jingle to next note
            }
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[RESTART] P1 starts new game"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[RESTART] P2 starts new game"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_WIN_BLINK;
                // Determine which side the winner is on
                uint8_t p2_side = (thestate == ST_WIN_R || thestate == ST_FORFEIT_L);
                if (!animate_win(p2_side)) {
                    // Animation complete → return to idle
                    Serial.println(F("[WIN] animation done → idle"));
                    set_state(ST_IDLE);
                }
            }
            break;

        default:
            Serial.println(F("[ERROR] unknown state – resetting to idle"));
            set_state(ST_IDLE);
            break;
    }

    // ── Global tone-off: silence buzzer when note duration expires ──
    // This fires for every state EXCEPT win/forfeit, which consume
    // EV_TONETIMER above to advance the jingle instead of silencing it.
    if (chk_ev(EV_TONETIMER))
        set_tone(0, 0);
}

// vim: syn=cpp
