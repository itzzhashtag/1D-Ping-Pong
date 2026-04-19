 /*
 * ============================================================
 *  1D LED PONG  v1.6  –  Arduino Uno
 * ============================================================
 *  LIBRARIES REQUIRED (Arduino Library Manager):
 *    • Adafruit NeoPixel
 *    • TM1637Display  (by avishorp)
 *
 *  WIRING:
 *    Pin  2  → WS2812B DIN  (330Ω series resistor recommended)
 *    Pin  3  → Passive buzzer – lead (– to GND)
 *    Pin  4  → P1 Hit   button → GND   (INPUT_PULLUP, active LOW)
 *    Pin  5  → P1 Power button → GND
 *    Pin  6  → P2 Hit   button → GND
 *    Pin  7  → P2 Power button → GND
 *    Pin  8  → TM1637 #1 CLK  (P1 display, left)
 *    Pin  9  → TM1637 #1 DIO
 *    Pin 10  → TM1637 #2 CLK  (P2 display, right)
 *    Pin 11  → TM1637 #2 DIO
 *    5V / GND → strip, both displays, buzzer common rail
 *
 * ── CHANGES FROM v2.1 ──────────────────────────────────────
 *  1. Forfeit (Hit+Power hold) timer REMOVED.
 *     Power button is no longer blocked by any hold-timer.
 *
 *  2. Serve-after-point rule:
 *     After losing a point the scorer MUST press Hit to start
 *     the next ball (Power is ignored on that first serve).
 *     Once the ball is live, both Hit and Power work freely —
 *     no lockout timer between presses during a rally.
 *
 *  3. Hit zone is the FULL cyan region (all ZONE_SIZE LEDs).
 *     Player can return the ball anywhere inside the cyan strip.
 *     Ball only scores past the last cyan LED (pixel 0 or
 *     NPIXELS-1) — it does NOT score at the zone boundary.
 *     Power-boost still available mid-rally via Hit+Power held.
 *
 *  4. Serial output cleaned up: each event prints ONCE per
 *     occurrence.  No repeated prints inside the tick loop.
 *
 *  5. Full inline comments throughout.
 *     Top-of-file CONFIG section for easy tuning.
 * ============================================================
 */

// ============================================================
//  CONFIG  –  tune everything from here
// ============================================================

// ── Hardware pins ────────────────────────────────────────────
#define LED_PIN     2   // WS2812B data line
#define BUZZER      3   // Passive buzzer (tone() on any digital pin)
#define P1_HIT      4   // P1 Hit   button (INPUT_PULLUP, active LOW)
#define P1_POWER    5   // P1 Power button
#define P2_HIT      6   // P2 Hit   button
#define P2_POWER    7   // P2 Power button
#define TM1_CLK     8   // TM1637 display #1 clock  (P1, left side)
#define TM1_DIO     9   // TM1637 display #1 data
#define TM2_CLK    10   // TM1637 display #2 clock  (P2, right side)
#define TM2_DIO    11   // TM1637 display #2 data

// ── Strip layout ─────────────────────────────────────────────
#define NPIXELS     60  // Total WS2812B LEDs in the strip
#define ZONE_SIZE    7  // Width (LEDs) of each player's cyan hit zone

// ── Scoring ──────────────────────────────────────────────────
#define WIN_POINTS   9  // Points needed to win the game

// ── Ball speed  (ms per LED step; LOWER = FASTER) ────────────
#define TIME_SPEED_MIN       10  // Absolute fastest step (floor)
#define TIME_SPEED_INTERVAL   3  // ms subtracted per LED distance from wall
//   Normal serve speed = TIME_SPEED_MIN + SERVE_DIST * TIME_SPEED_INTERVAL
#define SERVE_DIST            5  // Distance factor used for the initial serve

// ── Power serve randomness ───────────────────────────────────
//   Power serve picks a random speed between these two values.
//   Wider gap = more chaos; narrower = more consistent power serve.
#define POWER_SERVE_MIN  TIME_SPEED_MIN                              // fastest
#define POWER_SERVE_MAX  (TIME_SPEED_MIN + 8 * TIME_SPEED_INTERVAL) // slowest

// ── Power-boost multiplier during a rally ────────────────────
//   When player presses Hit + holds Power in their zone the ball
//   travels at  speed * BOOST_NUM / BOOST_DEN  (default: 75%).
#define BOOST_NUM  3
#define BOOST_DEN  4

// ── Display brightness (0-7) ─────────────────────────────────
#define DISP_BRIGHTNESS  5

// ── LED brightness levels ────────────────────────────────────
#define SHOW_LO  12   // Score-bar brightness while ball is in flight
#define SHOW_HI  48   // Score-bar brightness at rest / serve

// ── Timing (ms) ──────────────────────────────────────────────
#define TIME_DEBOUNCE       8    // Button debounce window
#define TIME_IDLE          40    // Idle animation tick period (~25 fps)
#define TIME_START_TIMEOUT 20000 // Auto-return to idle if no serve (first serve only)
#define TIME_BALL_BLINK    150   // Serve-blink half-period
#define TIME_POINT_BLINK   233   // New-score-dot blink period
#define TIME_WIN_BLINK      85   // Win animation frame period
#define TIME_LOCKOUT       250   // Anti-mash gate between accepted presses
#define TONE_INTERVAL        5   // Fire a move-tick sound every N ball steps

// ── Sound frequencies (Hz) ───────────────────────────────────
#define SND_BOUNCE       196  // G3  – ball returned
#define SND_TICK         392  // G4  – ball in-flight tick
#define SND_SCORE        523  // C5  – point scored
#define SND_SERVE        174  // F3  – normal serve
#define SND_POWER_SERVE  220  // A3  – power serve (hint of chaos)

// ── Sound durations (ms) ─────────────────────────────────────
#define TIME_TONE_BOUNCE  50
#define TIME_TONE_MOVE    25
#define TIME_TONE_SCORE   50
#define TIME_TONE_SERVE   50

// ── Idle-animation hue steps ─────────────────────────────────
#define H_STEPS  1542   // One full hue revolution in our unit space

// ── Score bar (derived, do not edit) ─────────────────────────
#define SCORE_PIXELS  (NPIXELS / 2 - ZONE_SIZE)  // LEDs available per side
#define LEDS_PER_PT   (SCORE_PIXELS / WIN_POINTS) // LEDs per score point

// ============================================================
//  END CONFIG
// ============================================================

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>

// Handy element-count macro (used for the win jingle array)
#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// Pin aliases used inside the legacy FSM (keep these matching the CONFIG above)
#define PIN_WSDATA   LED_PIN
#define PIN_SOUND    BUZZER
#define PIN_BUT_LS   P1_HIT
#define PIN_BUT_LP   P1_POWER
#define PIN_BUT_RS   P2_HIT
#define PIN_BUT_RP   P2_POWER

// ── Event-flag bit-mask (returned by do_debounce / do_timer) ─
#define EV_BUT_LS_PRESS  0x01  // P1 Hit   pressed (falling edge)
#define EV_BUT_RS_PRESS  0x02  // P2 Hit   pressed
#define EV_BUT_LP_PRESS  0x04  // P1 Power pressed
#define EV_BUT_RP_PRESS  0x08  // P2 Power pressed
#define EV_TIMER         0x10  // General FSM countdown expired
#define EV_TIMEOUT       0x20  // Start-screen idle timeout expired
#define EV_TONETIMER     0x40  // Current buzzer-note duration expired

// ── 7-segment letter bitmaps ─────────────────────────────────
//   Segment layout: bit6=g(middle) 5=f 4=e 3=d 2=c 1=b 0=a(top)
//   "WOn " → winner display   "dEAd" → loser display
//   "KnS " → forfeiter        "----" → idle / no game
#define SEG_DASH  0x40  // –
#define SEG_BLNK  0x00  // (blank)
#define SEG_A     0x77  // A
#define SEG_d     0x5E  // d
#define SEG_E     0x79  // E
#define SEG_H     0x76  // H  (best approx for K on 7-seg)
#define SEG_n     0x54  // n  (best approx for N/M on 7-seg)
#define SEG_O     0x3F  // O
#define SEG_S     0x6D  // S
#define SEG_U     0x3E  // U  (best approx for W on 7-seg)

// ── Game states ──────────────────────────────────────────────
enum {
    ST_IDLE = 0,        // No game – rainbow idle animation on LEDs
    ST_START_L,         // P1 must serve to begin (first serve of game)
    ST_START_R,         // P2 must serve to begin
    ST_MOVE_LR,         // Ball flying left → right
    ST_MOVE_RL,         // Ball flying right → left
    ST_ZONE_L,          // Ball inside P1's cyan hit zone
    ST_ZONE_R,          // Ball inside P2's cyan hit zone
    ST_POINT_L,         // P1 just scored – score-dot blink phase
    ST_POINT_R,         // P2 just scored
    ST_RESUME_L,        // After P1 scores, P1 re-serves (Hit only)
    ST_RESUME_R,        // After P2 scores, P2 re-serves (Hit only)
    ST_WIN_L,           // P1 reached WIN_POINTS – game over
    ST_WIN_R,           // P2 reached WIN_POINTS – game over
    ST_FORFEIT_L,       // (kept for future use; forfeit mechanic removed)
    ST_FORFEIT_R,
};

// ── Library objects ──────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, LED_PIN, NEO_GRB | NEO_KHZ800);
TM1637Display     disp1(TM1_CLK, TM1_DIO);   // P1 display (left)
TM1637Display     disp2(TM2_CLK, TM2_DIO);   // P2 display (right)

// ── Win jingle: {frequency Hz, duration ms} pairs in flash ───
typedef struct { uint16_t freq; uint16_t dur; } note_t;
static const note_t tune_win[] PROGMEM = {
    {1661,125},{1760,125},{1661,125},{1319,125},
    {1661,125},{1760,125},{1661,125},{1319,125},
    {   0,250},
    { 294,250},{ 294,250},{ 247,250},{ 330,250},
    { 294,500},{ 247,500},
    { 294,250},{ 294,250},{ 247,250},{ 330,250},
    { 294,500},{ 247,500},
};

// ── Global game state variables ──────────────────────────────
static uint32_t oldtime;       // millis() snapshot from last loop tick
static uint8_t  thestate;      // current FSM state (one of the ST_* enums)

// Debounce: bstate = settled pin level; debtmr = settle countdown
static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

static uint16_t timer;         // General FSM countdown (ms)
static uint16_t timeout;       // 20-s idle timeout for first serve
static uint16_t tonetimer;     // Remaining ms of current buzzer note
static uint16_t lockout_l;     // P1 anti-mash countdown (between presses)
static uint16_t lockout_r;     // P2 anti-mash countdown

static uint8_t  ballblinkstate;   // Blink toggle while ball waits to serve
static uint8_t  pointblinkcount;  // Blink cycles remaining after point scored
static uint8_t  ballpos;          // Current ball LED index (0 … NPIXELS-1)
static uint16_t speed;            // ms per ball step (lower = faster)
static uint8_t  speedup;          // Accumulated rally speed bonus (presses)
static uint8_t  points_l;         // P1 score
static uint8_t  points_r;         // P2 score
static uint8_t  zone_l, zone_r;   // Live hit-zone widths (shrink with boost)
static uint8_t  boost_l, boost_r; // Boost uses this rally (P1 / P2)
static uint8_t  boosted;          // 1 while ball is travelling at boost speed
static uint8_t  tonecount;        // Steps until next move-tick beep
static uint8_t  tuneidx;          // Position inside tune_win[] array
static uint8_t  aw_state;         // Win animation frame counter

// ── Idle animation sub-state ─────────────────────────────────
static uint16_t ai_h;          // Current hue position
static uint8_t  ai_state;      // Phase: 0=fade-in 1-3=rainbow 4-7=ball 8-11=fill 12=fade-out
static uint8_t  ai_pos;        // Scratch: ball position or fill extent
static uint8_t  ai_brightness; // Rainbow brightness for fade in/out phases

// ============================================================
//  SOUND FUNCTIONS
// ============================================================

/*
 * set_tone(freq, duration)
 *  Start the buzzer at freq Hz for duration ms.
 *  freq = 0 → silence immediately.  Returns at once (non-blocking).
 */
static inline void set_tone(uint16_t freq, uint16_t duration) {
    tonetimer = duration;
    if (freq) tone(PIN_SOUND, freq);
    else      noTone(PIN_SOUND);
}

/*
 * tune_next()
 *  Play the next note of the win jingle and advance tuneidx.
 *  Called each time EV_TONETIMER fires while in ST_WIN_* or ST_FORFEIT_*.
 */
static inline void tune_next() {
    if (tuneidx < NELEM(tune_win)) {
        uint16_t f = pgm_read_word(&tune_win[tuneidx].freq);
        uint16_t d = pgm_read_word(&tune_win[tuneidx].dur);
        set_tone(f, d);
        tuneidx++;
        // Print note index and frequency once per note advance
        Serial.print(F("[TUNE] note "));
        Serial.print(tuneidx);
        Serial.print(F("/"));
        Serial.print((uint8_t)NELEM(tune_win));
        Serial.print(F("  freq="));
        Serial.println(f);
    } else {
        set_tone(0, 0);
        Serial.println(F("[TUNE] jingle finished"));
    }
}

// ============================================================
//  DISPLAY HELPERS  (dual TM1637)
// ============================================================

// Show "----" on a single display (idle, no game)
static void disp_dashes(TM1637Display &d) {
    uint8_t seg[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
    d.setSegments(seg, 4, 0);
}

// Show "----" on both displays simultaneously
static void disp_idle_both() {
    disp_dashes(disp1);
    disp_dashes(disp2);
}

/*
 * disp_score_on(d, score)
 *  Show score right-justified in 2 digits: "  0X"
 *  Positions 0-1 blank; positions 2-3 are tens/units.
 */
static void disp_score_on(TM1637Display &d, uint8_t score) {
    uint8_t seg[4] = {
        SEG_BLNK,
        SEG_BLNK,
        d.encodeDigit(score / 10),
        d.encodeDigit(score % 10)
    };
    d.setSegments(seg, 4, 0);
}

// Refresh both displays with the current live scores
static void disp_scores() {
    disp_score_on(disp1, points_l);
    disp_score_on(disp2, points_r);
}

// "WOn " – winner's display
static void disp_win_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_U, SEG_O, SEG_n, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

// "dEAd" – loser's display
static void disp_dead_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_d, SEG_E, SEG_A, SEG_d};
    d.setSegments(seg, 4, 0);
}

// "KnS " – forfeiter's display (K≈H, M≈n on 7-seg)
static void disp_kms_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_H, SEG_n, SEG_S, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

// ============================================================
//  BUTTON HELPERS
// ============================================================

/*
 * button_is_down(pin)
 *  Returns 1 if the debounced state of pin is currently pressed.
 *  Used for simultaneous Hit+Power detection (power boost, forfeit).
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
 * do_debounce(tdiff, bstate, debtmr, pin, ev)
 *  Filters contact bounce.  Fires ev ONCE on the falling edge
 *  (HIGH→LOW = button pressed).  INPUT_PULLUP: HIGH=released, LOW=pressed.
 */
static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
    if (!*debtmr) {
        uint8_t state = digitalRead(pin);
        if (state != *bstate) {
            *debtmr = TIME_DEBOUNCE;           // begin settle window
            if (!(*bstate = state)) return ev; // HIGH→LOW = press event
        }
    } else {
        // Count down the settle window
        if (*debtmr >= tdiff) *debtmr -= tdiff;
        else                  *debtmr = 0;
    }
    return 0;
}

/*
 * do_timer(tdiff, tmr, ev)
 *  Counts a uint16_t timer down to zero; fires ev exactly once.
 */
static inline uint8_t do_timer(uint8_t tdiff, uint16_t *tmr, uint8_t ev) {
    if (*tmr) {
        if (*tmr >= tdiff) *tmr -= tdiff;
        else               *tmr = 0;
        if (!*tmr) return ev;
    }
    return 0;
}

// ============================================================
//  POWER SERVE – randomised "human-level" launch speed
// ============================================================

/*
 * power_serve_speed()
 *  Returns a random speed (ms/step) in [POWER_SERVE_MIN, POWER_SERVE_MAX].
 *  Wide spread → opponent cannot read the serve timing from LED position.
 *  Logs the chosen speed once per serve.
 */
static uint16_t power_serve_speed() {
    uint16_t range = POWER_SERVE_MAX - POWER_SERVE_MIN + 1;
    uint16_t s = POWER_SERVE_MIN + (uint16_t)random(range);
    Serial.print(F("[POWER-SERVE] random speed="));
    Serial.print(s);
    Serial.println(F(" ms/step"));
    return s;
}

// ============================================================
//  LED DRAW HELPERS
// ============================================================

/*
 * draw_sides()
 *  Paint the cyan hit zones at both ends of the strip.
 *  zone_l / zone_r can shrink when a boost is active.
 */
static void draw_sides() {
    for (uint8_t i = 0; i < zone_l; i++)
        one_d.setPixelColor(i, 0, 64, 64);                     // left zone: teal
    for (uint8_t i = 0; i < zone_r; i++)
        one_d.setPixelColor(NPIXELS - 1 - i, 0, 64, 64);       // right zone: teal
}

/*
 * draw_ball(dir, pos)
 *  Yellow ball at pos with a 5-LED diminishing tail.
 *  dir = +1 → tail trails leftward (ball going right)
 *  dir = -1 → tail trails rightward (ball going left)
 */
static void draw_ball(int8_t dir, uint8_t pos) {
    uint8_t c = 255;
    for (uint8_t i = 0; i < 5 && pos < NPIXELS; i++) {
        one_d.setPixelColor(pos, c, c, 0);  // yellow, dimming
        c >>= 1;
        pos -= dir;   // tail goes opposite to travel direction
    }
}

/*
 * draw_course(v)
 *  Clear strip → paint zones → paint score bars.
 *  Score bar:  P1 red  dots grow leftward  from centre (NPIXELS/2 - 1)
 *              P2 green dots grow rightward from centre (NPIXELS/2)
 *  v = SHOW_LO during flight (dim), SHOW_HI at rest, 0 to skip bars.
 */
static void draw_course(uint8_t v) {
    one_d.clear();
    draw_sides();
    if (v) {
        // P1 score: red dots left of centre
        for (uint8_t i = 0; i < points_l; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 - 1 - (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, v, 0, 0);
            }
        // P2 score: green dots right of centre
        for (uint8_t i = 0; i < points_r; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 + (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, 0, v, 0);
            }
    }
}

// ============================================================
//  IDLE ANIMATION
// ============================================================

static void animate_idle_init() {
    ai_h = 0; ai_state = 0; ai_pos = 0; ai_brightness = 0;
    Serial.println(F("[IDLE] animation reset → fade-in"));
}

// Render rainbow across full strip at given brightness; ai_h not advanced here.
static void draw_rainbow(uint8_t brightness) {
    for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        // Map H_STEPS range to ColorHSV 0-65535 range (ratio ≈ 42)
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, brightness));
    }
}

/*
 * animate_idle()
 *  Called every TIME_IDLE ms while in ST_IDLE.
 *  Phases: 0=fade-in, 1-3=full rainbow, 4-7=demo ball, 8-11=score fill, 12=fade-out → loops.
 */
static void animate_idle() {
    switch (ai_state) {

        // Phase 0: fade brightness 0 → 128 while rainbow rotates
        case 0:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness < 124) { ai_brightness += 4; }
            else { ai_brightness = 128; ai_state = 1;
                   Serial.println(F("[IDLE] fade-in done")); }
            break;

        // Phases 1-3: full rainbow, 3 complete hue rotations
        case 1: case 2: case 3:
            draw_rainbow(128);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) { ai_h -= H_STEPS; ai_pos = 0; ai_state++; }
            break;

        // Phases 4,6: demo ball L → R
        case 4: case 6:
            draw_course(0); draw_ball(+1, ai_pos++);
            if (ai_pos >= NPIXELS) ai_state++;
            break;

        // Phases 5,7: demo ball R → L
        case 5: case 7:
            draw_course(0); draw_ball(-1, --ai_pos);
            if (!ai_pos) ai_state++;
            break;

        // Phases 8,10: score fill expanding outward from centre
        case 8: case 10:
            draw_course(0);
            for (uint8_t i = 0; i < ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);  // red left
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);  // green right
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;

        // Phases 9,11: score fill collapsing inward
        case 9: case 11:
            draw_course(0);
            for (uint8_t i = 0; i < NPIXELS / 2 - ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;

        // Phase 12: fade brightness 128 → 0 then loop back to phase 0
        case 12:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness > 4) { ai_brightness -= 4; }
            else { ai_brightness = 0; ai_state = 0;
                   Serial.println(F("[IDLE] fade-out → looping")); }
            break;

        default:
            ai_state = 0; ai_brightness = 0;
            break;
    }
    one_d.show();
}

// ============================================================
//  WIN ANIMATION
// ============================================================

static void animate_win_init() { aw_state = 0; }

/*
 * animate_win(side)
 *  Multi-phase wipe over the winner's half of the strip.
 *  side 0 → P1 wins (left half, red)
 *  side 1 → P2 wins (right half, green)
 *  Returns 1 while still running, 0 when done.
 */
static uint8_t animate_win(uint8_t side) {
    uint32_t clr;
    uint8_t  pos;
    if (side) { clr = Adafruit_NeoPixel::Color(0, 255, 0); pos = NPIXELS / 2; }
    else      { clr = Adafruit_NeoPixel::Color(255, 0, 0); pos = 0; }
    one_d.clear();
    if      (aw_state < 20)  { if (aw_state & 1) for (uint8_t i=0;i<NPIXELS/2;i++) one_d.setPixelColor(pos+i, clr); }
    else if (aw_state < 50)  { for (uint8_t i=0;            i<aw_state-20;  i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state < 80)  { for (uint8_t i=aw_state-50;  i<NPIXELS/2;   i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state < 110) { for (uint8_t i=0;            i<aw_state-80;  i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    else if (aw_state < 140) { for (uint8_t i=aw_state-110; i<NPIXELS/2;   i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    one_d.show();
    return ++aw_state < 140;
}

// ============================================================
//  STATE MACHINE HELPERS
// ============================================================

/*
 * is_game_state(s)
 *  Returns 1 for states where the lockout gate should be active.
 *  Prevents accidental repeated input during flight / score / end.
 *  NOTE: ZONE states are intentionally excluded so there is NO
 *  lockout during a rally – Hit and Power are always live.
 */
static uint8_t is_game_state(uint8_t s) {
    switch (s) {
        case ST_MOVE_LR:   case ST_MOVE_RL:   // ball in flight
        case ST_POINT_L:   case ST_POINT_R:   // post-point blink
        case ST_WIN_L:     case ST_WIN_R:     // game over
        case ST_FORFEIT_L: case ST_FORFEIT_R: // forfeit (future)
            return 1;
        default:
            return 0;
    }
}

/*
 * speed_to_timer()
 *  Load `timer` with the current ball step interval.
 *  If the ball is boosted it travels at  speed * BOOST_NUM/BOOST_DEN.
 *  Floor is 2 ms to avoid zero-timer lockup.
 */
static inline void speed_to_timer() {
    timer = boosted ? (uint16_t)speed * BOOST_NUM / BOOST_DEN : speed;
    if (timer < 2) timer = 2;
}

// ============================================================
//  SERVE HELPERS
//  Centralise all serve logic so ST_START and ST_RESUME both
//  call the same path.  power=0 → normal; power=1 → random.
// ============================================================

/*
 * do_serve_L(power)
 *  Launch ball leftward (P1 served toward P2).
 *  Normal: uses speed set in EXIT of START_L / RESUME_L.
 *  Power:  picks a random speed (unpredictable, "human").
 */
static void do_serve_L(uint8_t power) {
    if (power) {
        speed = power_serve_speed();
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1 POWER – random speed"));
    } else {
        // speed already set by EXIT action; just play the sound
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1 normal"));
    }
    boosted = 0;
    set_state(ST_MOVE_LR);  // ball now travels left→right
}

/*
 * do_serve_R(power)
 *  Launch ball rightward (P2 served toward P1).
 */
static void do_serve_R(uint8_t power) {
    if (power) {
        speed = power_serve_speed();
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2 POWER – random speed"));
    } else {
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2 normal"));
    }
    boosted = 0;
    set_state(ST_MOVE_RL);  // ball now travels right→left
}

// ============================================================
//  STATE TRANSITIONS
// ============================================================

/*
 * set_state(newstate)
 *  Runs EXIT actions for thestate, then ENTRY actions for newstate.
 *  All LED/display/sound/variable setup on a state change lives here.
 */
static void set_state(uint8_t newstate) {

    // ── Print transition once ──────────────────────────────────
    Serial.print(F("[STATE] "));
    Serial.print(thestate);
    Serial.print(F(" → "));
    Serial.println(newstate);

    // ── EXIT ACTIONS ──────────────────────────────────────────
    switch (thestate) {

        // Entering a new game: zero everything
        case ST_IDLE:
        case ST_WIN_L: case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            points_l = points_r = 0;
            boost_l  = boost_r  = 0;
            zone_l   = zone_r   = ZONE_SIZE;
            speedup  = 0;
            boosted  = 0;
            Serial.println(F("[RESET] new game – scores/zones/boosts cleared"));
            break;

        // P1 side serve: ball starts at pixel 0, set default speed
        case ST_START_L:
        case ST_POINT_L:
        case ST_RESUME_L:
            ballpos = 0;
            speed   = TIME_SPEED_MIN + SERVE_DIST * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P2 side serve: ball starts at last pixel
        case ST_START_R:
        case ST_POINT_R:
        case ST_RESUME_R:
            ballpos = NPIXELS - 1;
            speed   = TIME_SPEED_MIN + SERVE_DIST * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P1 just hit: speed based on how deep in the zone they hit
        // Closer to the wall = more time to line up = faster shot
        case ST_ZONE_L:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] P1 return – speed="));
            Serial.print(speed);
            Serial.println(F(" ms/step"));
            break;

        // P2 just hit: same logic, distance from right wall
        case ST_ZONE_R:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS - 1 - ballpos);
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] P2 return – speed="));
            Serial.print(speed);
            Serial.println(F(" ms/step"));
            break;
    }

    thestate = newstate;

    // ── ENTRY ACTIONS ─────────────────────────────────────────
    switch (thestate) {

        // ── Idle: rainbow + "----" on displays ───────────────
        case ST_IDLE:
            boost_l = boost_r = 0;
            zone_l  = zone_r  = ZONE_SIZE;
            animate_idle_init();
            timer = TIME_IDLE;
            disp_idle_both();
            Serial.println(F("[IDLE] rainbow playing – any button starts game"));
            break;

        // ── First serve screen (ball blinks, 20s auto-timeout) ─
        //    EITHER button wakes from idle, but Hit or Power can serve.
        case ST_START_L:
        case ST_START_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = TIME_START_TIMEOUT;   // 20s then back to idle
            ballblinkstate = 0;
            ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;
            disp_scores();
            Serial.print(thestate == ST_START_L ? F("[START] P1") : F("[START] P2"));
            Serial.println(F(" – Hit=normal  Power=random  (20s timeout)"));
            break;

        // ── Ball in flight: arm timer, show dim score bars ───
        case ST_MOVE_LR:
        case ST_MOVE_RL:
            speed_to_timer();
            tonecount = TONE_INTERVAL;
            disp_scores();
            Serial.print(F("[BALL] dir="));
            Serial.print(thestate == ST_MOVE_LR ? F("L→R") : F("R→L"));
            Serial.print(F("  speed="));
            Serial.print(speed);
            Serial.println(F(" ms/step"));
            break;

        // ── Post-point blink ──────────────────────────────────
        case ST_POINT_L:
        case ST_POINT_R:
            pointblinkcount = 7;
            // Recover one zone LED for a player who did NOT boost this rally
            if (!boost_l && zone_l < ZONE_SIZE) zone_l++;
            if (!boost_r && zone_r < ZONE_SIZE) zone_r++;
            timer     = TIME_POINT_BLINK;
            if (boost_l) boost_l--;
            if (boost_r) boost_r--;
            lockout_l = lockout_r = TIME_LOCKOUT;   // brief mash gate after point
            disp_scores();
            Serial.print(F("[SCORE] P1="));
            Serial.print(points_l);
            Serial.print(F("  P2="));
            Serial.print(points_r);
            Serial.print(F("  (first to "));
            Serial.print(WIN_POINTS);
            Serial.println(F(" wins)"));
            break;

        // ── Re-serve screen (scorer must press Hit – Power blocked here) ──
        //    No auto-timeout: scorer must manually serve.
        case ST_RESUME_L:
        case ST_RESUME_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = 0;           // no auto-fire
            ballblinkstate = 0;
            disp_scores();
            Serial.print(thestate == ST_RESUME_L ? F("[WAIT] P1") : F("[WAIT] P2"));
            Serial.println(F(" – press Hit to re-serve (Power blocked on first re-serve)"));
            break;

        // ── P1 wins ───────────────────────────────────────────
        case ST_WIN_L:
            disp_win_msg(disp1);   // "WOn "
            disp_dead_msg(disp2);  // "dEAd"
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[WIN] P1 wins!  P1:'WOn ' P2:'dEAd'"));
            Serial.print(F("[WIN] Final  P1="));
            Serial.print(points_l);
            Serial.print(F("  P2="));
            Serial.println(points_r);
            break;

        // ── P2 wins ───────────────────────────────────────────
        case ST_WIN_R:
            disp_dead_msg(disp1);  // "dEAd"
            disp_win_msg(disp2);   // "WOn "
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[WIN] P2 wins!  P1:'dEAd' P2:'WOn '"));
            Serial.print(F("[WIN] Final  P1="));
            Serial.print(points_l);
            Serial.print(F("  P2="));
            Serial.println(points_r);
            break;

        // ── Forfeit states kept for completeness, no timer mechanic ──
        case ST_FORFEIT_L:
            disp_kms_msg(disp1);
            disp_win_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[FORFEIT] P1 out – P2 wins"));
            break;

        case ST_FORFEIT_R:
            disp_win_msg(disp1);
            disp_kms_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[FORFEIT] P2 out – P1 wins"));
            break;
    }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
    Serial.begin(9600);
    Serial.println(F("===================================="));
    Serial.println(F("  1D PONG  v2.2  –  STARTUP"));
    Serial.println(F("===================================="));
    Serial.print(F("  NPIXELS      = ")); Serial.println(NPIXELS);
    Serial.print(F("  ZONE_SIZE    = ")); Serial.println(ZONE_SIZE);
    Serial.print(F("  WIN_POINTS   = ")); Serial.println(WIN_POINTS);
    Serial.print(F("  LEDS_PER_PT  = ")); Serial.println(LEDS_PER_PT);
    Serial.print(F("  SPEED_MIN    = ")); Serial.print(TIME_SPEED_MIN); Serial.println(F(" ms"));
    Serial.print(F("  POWER SERVE  = ")); Serial.print(POWER_SERVE_MIN);
    Serial.print(F(" – ")); Serial.print(POWER_SERVE_MAX); Serial.println(F(" ms (random)"));
    Serial.println(F("  [Forfeit mechanic REMOVED in v2.2]"));
    Serial.println(F("  Re-serve after point: Hit only (Power blocked once)"));
    Serial.println(F("  During rally: Hit OR Power freely, no lockout"));

    // Seed the RNG from floating ADC so power serves differ each game
    randomSeed(analogRead(A0));

    // Pull all unused port pins HIGH to avoid floating inputs causing noise
    PORTB = PORTC = PORTD = 0xFF;

    // Button inputs – HIGH at rest, pulled LOW when pressed (active LOW)
    pinMode(P1_HIT,   INPUT_PULLUP);
    pinMode(P1_POWER, INPUT_PULLUP);
    pinMode(P2_HIT,   INPUT_PULLUP);
    pinMode(P2_POWER, INPUT_PULLUP);

    // Buzzer starts silent
    digitalWrite(BUZZER, LOW);
    pinMode(BUZZER, OUTPUT);

    // TM1637 displays
    disp1.setBrightness(DISP_BRIGHTNESS);
    disp2.setBrightness(DISP_BRIGHTNESS);
    disp_idle_both();   // show "----" immediately

    // WS2812B strip – all LEDs off
    one_d.begin();
    one_d.show();

    // Start the FSM in idle
    thestate = ST_IDLE;
    set_state(ST_IDLE);

    Serial.println(F("===================================="));
    Serial.println(F("  Ready!  Press any button to start"));
    Serial.println(F("===================================="));
}

// ============================================================
//  MAIN LOOP
// ============================================================

#define chk_ev(ev)  (events & (ev))

void loop() {
    uint32_t now;
    uint8_t tdiff  = (now = millis()) - oldtime;
    uint8_t events = 0;

    // ── Tick all debouncers and timers once per ms ───────────
    if (tdiff) {
        oldtime = now;

        // Debounce: fire one event per falling edge (button press)
        events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, P1_HIT,   EV_BUT_LS_PRESS);
        events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, P2_HIT,   EV_BUT_RS_PRESS);
        events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, P1_POWER, EV_BUT_LP_PRESS);
        events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, P2_POWER, EV_BUT_RP_PRESS);

        // General timers
        events |= do_timer(tdiff, &timer,     EV_TIMER);
        events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
        events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);

        // Lockout just counts down; no event needed
        do_timer(tdiff, &lockout_l, 0);
        do_timer(tdiff, &lockout_r, 0);
    }

    // ── Log button edges once (before any state consumes them) ─
    if (chk_ev(EV_BUT_LS_PRESS)) Serial.println(F("[BTN] P1 Hit"));
    if (chk_ev(EV_BUT_RS_PRESS)) Serial.println(F("[BTN] P2 Hit"));
    if (chk_ev(EV_BUT_LP_PRESS)) Serial.println(F("[BTN] P1 Power"));
    if (chk_ev(EV_BUT_RP_PRESS)) Serial.println(F("[BTN] P2 Power"));

    // ── Lockout gate: suppress Hit events in flight/score/end ─
    //    Zone states are NOT listed in is_game_state(), so there
    //    is NO suppression during a rally – both buttons live.
    if (is_game_state(thestate)) {
        if (lockout_l) events &= ~EV_BUT_LS_PRESS;
        if (lockout_r) events &= ~EV_BUT_RS_PRESS;
    }
    // Re-arm lockout whenever a Hit press passes the gate
    if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
    if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;

    // ── State machine ─────────────────────────────────────────
    switch (thestate) {

        // ── IDLE: rainbow plays; any button starts ────────────
        case ST_IDLE:
            if (chk_ev(EV_BUT_LS_PRESS) || chk_ev(EV_BUT_LP_PRESS)) {
                // P1 started – always open with normal-serve screen
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS) || chk_ev(EV_BUT_RP_PRESS)) {
                // P2 started
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_IDLE;
                animate_idle();
            }
            break;

        // ── START_L: P1's first serve of the game ─────────────
        //    Hit → normal fixed speed
        //    Power → random "human" speed
        //    After 20s with no serve → back to idle
        case ST_START_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);                    // normal serve
            } else if (chk_ev(EV_BUT_LP_PRESS)) {
                do_serve_L(1);                    // power / random serve
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20s timeout → idle"));
                set_state(ST_IDLE);
            } else if (chk_ev(EV_TIMER)) {
                // Blink the serve-position LED orange while waiting
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── START_R: P2's first serve ─────────────────────────
        case ST_START_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                do_serve_R(0);
            } else if (chk_ev(EV_BUT_RP_PRESS)) {
                do_serve_R(1);
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20s timeout → idle"));
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

        // ── MOVE_LR: ball travelling left → right ─────────────
        case ST_MOVE_LR:
            if (chk_ev(EV_TIMER)) {
                // Periodic move-tick beep every TONE_INTERVAL steps
                if (!--tonecount) {
                    set_tone(SND_TICK, TIME_TONE_MOVE);
                    tonecount = TONE_INTERVAL;
                }
                speed_to_timer();
                draw_course(SHOW_LO);
                draw_ball(+1, ballpos);
                one_d.show();
                ballpos++;
                // Enter P2's zone when the ball is within zone_r LEDs of the wall
                if (NPIXELS - 1 - ballpos < zone_r) {
                    Serial.print(F("[BALL] entering P2 zone  pos="));
                    Serial.println(ballpos);
                    set_state(ST_ZONE_R);
                }
            }
            break;

        // ── MOVE_RL: ball travelling right → left ─────────────
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
                // Enter P1's zone when the ball is within zone_l LEDs of the wall
                if (ballpos < zone_l) {
                    Serial.print(F("[BALL] entering P1 zone  pos="));
                    Serial.println(ballpos);
                    set_state(ST_ZONE_L);
                }
            }
            break;

        // ── ZONE_L: ball inside P1's cyan hit zone ─────────────
        //
        //  The entire cyan region (zone_l LEDs) is a valid return window.
        //  Player can press Hit anywhere in the zone.
        //  Ball only scores when it passes pixel 0 (the physical wall).
        //  During a zone press: if Power is ALSO held → power boost.
        //  No lockout here – Hit and Power are always live mid-rally.
        case ST_ZONE_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                // ── Valid return anywhere inside the cyan zone ─
                Serial.print(F("[HIT] P1 at pos="));
                Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);

                // Power boost: only if Power is held AND zone can still shrink
                if (zone_l > 1 && button_is_down(P1_POWER)) {
                    zone_l--;           // shrink zone by 1 LED (permanent this rally)
                    boosted = 1;        // flag for speed_to_timer()
                    boost_l++;          // remember P1 used boost (prevents zone recovery)
                    Serial.println(F("[BOOST] P1 power boost – zone shrunk"));
                }

                set_state(ST_MOVE_LR); // ball now heads right
                speed_to_timer();      // apply boost if active (must come after set_state sets timer)

            } else if (chk_ev(EV_TIMER)) {
                // Ball keeps moving toward the wall while player hasn't returned it
                if (!ballpos) {
                    // Ball hit pixel 0 (P1's physical wall) → P2 scores
                    Serial.println(F("[MISS] P1 missed – P2 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_r >= WIN_POINTS) set_state(ST_WIN_R);
                    else                          set_state(ST_POINT_R);
                } else {
                    // Still inside zone, keep rolling
                    speed_to_timer();
                    ballpos--;
                    draw_course(SHOW_LO);
                    draw_ball(-1, ballpos);
                    one_d.show();
                }
            }
            break;

        // ── ZONE_R: ball inside P2's cyan hit zone ─────────────
        case ST_ZONE_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.print(F("[HIT] P2 at pos="));
                Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);

                if (zone_r > 1 && button_is_down(P2_POWER)) {
                    zone_r--;
                    boosted = 1;
                    boost_r++;
                    Serial.println(F("[BOOST] P2 power boost – zone shrunk"));
                }

                set_state(ST_MOVE_RL);
                speed_to_timer();

            } else if (chk_ev(EV_TIMER)) {
                if (ballpos == NPIXELS - 1) {
                    // Ball hit last pixel (P2's physical wall) → P1 scores
                    Serial.println(F("[MISS] P2 missed – P1 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_l >= WIN_POINTS) set_state(ST_WIN_L);
                    else                          set_state(ST_POINT_L);
                } else {
                    speed_to_timer();
                    ballpos++;
                    draw_course(SHOW_LO);
                    draw_ball(+1, ballpos);
                    one_d.show();
                }
            }
            break;

        // ── POINT_L: P1 scored – blink new score dot ──────────
        //    P1 can skip the blink early by pressing Hit.
        case ST_POINT_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                set_state(ST_RESUME_L);   // skip straight to serve screen
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                // Blink only the LEDs for the most recent P1 point
                for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                    uint8_t px = NPIXELS / 2 - 1 - ((points_l - 1) * LEDS_PER_PT + j);
                    one_d.setPixelColor(px, (pointblinkcount & 1) ? SHOW_HI : 0, 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_L);
            }
            break;

        // ── POINT_R: P2 scored ────────────────────────────────
        case ST_POINT_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                set_state(ST_RESUME_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                    uint8_t px = NPIXELS / 2 + ((points_r - 1) * LEDS_PER_PT + j);
                    one_d.setPixelColor(px, 0, (pointblinkcount & 1) ? SHOW_HI : 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_R);
            }
            break;

        // ── RESUME_L: P1 scored; P1 must re-serve with Hit ────
        //    Power is intentionally IGNORED here (blocked once).
        //    After the serve is live, both buttons work freely.
        case ST_RESUME_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);  // always normal speed on re-serve
            }
            // Power press silently ignored in this state (no else-if for LP)
            if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── RESUME_R: P2 scored; P2 must re-serve with Hit ────
        case ST_RESUME_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                do_serve_R(0);  // always normal speed on re-serve
            }
            if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── WIN & FORFEIT: jingle + animation, then restart ───
        //    Both states share identical loop logic.
        //    Win side: WIN_L→side0(red left)  WIN_R→side1(green right)
        //              FORFEIT_L→side1         FORFEIT_R→side0
        case ST_WIN_L: case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            // Advance jingle one note each time a note ends
            if (chk_ev(EV_TONETIMER)) {
                events &= ~EV_TONETIMER;   // consume so global handler won't silence it
                tune_next();
            }
            // Any Hit press restarts the game
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[RESTART] P1"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[RESTART] P2"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_WIN_BLINK;
                uint8_t p2_side = (thestate == ST_WIN_R || thestate == ST_FORFEIT_L);
                if (!animate_win(p2_side)) {
                    // Animation finished; return to idle
                    Serial.println(F("[WIN] animation done → idle"));
                    set_state(ST_IDLE);
                }
            }
            break;

        default:
            Serial.println(F("[ERROR] unknown state → idle"));
            set_state(ST_IDLE);
            break;
    }

    // ── Global tone-off ──────────────────────────────────────
    //    If EV_TONETIMER survived (not consumed by win/forfeit above),
    //    silence the buzzer — the note has run its duration.
    if (chk_ev(EV_TONETIMER))
        set_tone(0, 0);
}

// vim: syn=cpp
