/*
 * ============================================================
 *  1D LED PONG  v1.7  –  Arduino Uno
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
 * ── GAME RULES ─────────────────────────────────────────────
 *  • First serve  (ST_START_L / ST_START_R):
 *      Hit   → normal serve (fixed speed)
 *      Power → BLOCKED  (ignored)
 *
 *  • Re-serve after point  (ST_RESUME_L / ST_RESUME_R):
 *      Hit   → normal serve (fixed speed)
 *      Power → BLOCKED  (ignored)
 *
 *  • Rally returns  (ST_ZONE_L / ST_ZONE_R):
 *      Hit alone          → normal computed speed return
 *      Power alone        → RANDOM speed return  ("humanly unpredictable")
 *      Hit + Power held   → BOOST: normal speed × BOOST multiplier,
 *                           zone shrinks by 1 LED permanently for rally
 *
 * ── CHANGES FROM v1.6 ──────────────────────────────────────
 *
 *  ROOT CAUSE OF POWER-HIT BUG
 *  In ST_ZONE_L and ST_ZONE_R, the FSM only checked EV_BUT_LS_PRESS /
 *  EV_BUT_RS_PRESS (Hit).  EV_BUT_LP_PRESS / EV_BUT_RP_PRESS (Power)
 *  was never tested as a standalone return trigger – it could only
 *  fire as a modifier when Hit fired in the SAME tick.  So pressing
 *  Power alone in the zone did absolutely nothing.
 *
 *  FIX SUMMARY
 *  1. ZONE_L and ZONE_R now also check EV_BUT_LP_PRESS / EV_BUT_RP_PRESS
 *     as a valid ball-return event.
 *  2. Three sub-cases are resolved inside each ZONE handler:
 *       a) Power alone  → random speed (power_serve_speed()), no boost
 *       b) Hit alone    → normal computed speed, no boost
 *       c) Hit + Power  → boost multiplier + zone shrink (existing logic)
 *  3. RESUME states still block Power completely (unchanged, by design).
 *  4. START states still block Power completely (unchanged, by design).
 *  5. All comments and Serial prints preserved / expanded.
 * ============================================================
 * Wokwi Simulation : https://wokwi.com/projects/461754054622412801
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
//   Power serve / power return picks a random speed between these two
//   values.  Wide gap = more chaos; narrow = more consistent.
#define POWER_SERVE_MIN  TIME_SPEED_MIN                              // fastest possible
#define POWER_SERVE_MAX  (TIME_SPEED_MIN + 8 * TIME_SPEED_INTERVAL) // slowest possible

// ── Power-boost multiplier during a rally (Hit + Power held) ─
//   Ball travels at  speed * BOOST_NUM / BOOST_DEN  (default: 75% of step time = faster).
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
#define TIME_START_TIMEOUT 20000 // Auto-return to idle if no first serve
#define TIME_BALL_BLINK    150   // Serve-blink half-period
#define TIME_POINT_BLINK   233   // New-score-dot blink period
#define TIME_WIN_BLINK      85   // Win animation frame period
#define TIME_LOCKOUT       250   // Anti-mash gate between accepted presses
#define TONE_INTERVAL        5   // Fire a move-tick sound every N ball steps

// ── Sound frequencies (Hz) ───────────────────────────────────
#define SND_BOUNCE       196  // G3  – ball returned (normal hit)
#define SND_POWER_HIT    330  // E4  – ball returned (power hit alone) ← NEW
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

// ── Score bar (derived – do NOT edit these) ──────────────────
#define SCORE_PIXELS  (NPIXELS / 2 - ZONE_SIZE)  // LEDs available per side
#define LEDS_PER_PT   (SCORE_PIXELS / WIN_POINTS) // LEDs that light per point

// ============================================================
//  END CONFIG
// ============================================================

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>

// Handy element-count macro (used for the win jingle array)
#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// Pin aliases (keep matching CONFIG above)
#define PIN_WSDATA   LED_PIN
#define PIN_SOUND    BUZZER
#define PIN_BUT_LS   P1_HIT
#define PIN_BUT_LP   P1_POWER
#define PIN_BUT_RS   P2_HIT
#define PIN_BUT_RP   P2_POWER

// ── Event-flag bit-mask ──────────────────────────────────────
#define EV_BUT_LS_PRESS  0x01  // P1 Hit   pressed (falling edge)
#define EV_BUT_RS_PRESS  0x02  // P2 Hit   pressed
#define EV_BUT_LP_PRESS  0x04  // P1 Power pressed
#define EV_BUT_RP_PRESS  0x08  // P2 Power pressed
#define EV_TIMER         0x10  // General FSM countdown expired
#define EV_TIMEOUT       0x20  // Start-screen idle timeout expired
#define EV_TONETIMER     0x40  // Current buzzer-note duration expired

// ── 7-segment letter bitmaps ─────────────────────────────────
//   Bit layout: bit6=g(middle) 5=f 4=e 3=d 2=c 1=b 0=a(top)
#define SEG_DASH  0x40  // –
#define SEG_BLNK  0x00  // (blank)
#define SEG_A     0x77  // A
#define SEG_d     0x5E  // d
#define SEG_E     0x79  // E
#define SEG_H     0x76  // H  (closest to K on 7-seg)
#define SEG_n     0x54  // n  (closest to N on 7-seg)
#define SEG_O     0x3F  // O
#define SEG_S     0x6D  // S
#define SEG_U     0x3E  // U  (closest to W on 7-seg)

// ── Game states ──────────────────────────────────────────────
enum {
    ST_IDLE = 0,        // No game – rainbow idle animation
    ST_START_L,         // P1 first serve of the game  (Power BLOCKED)
    ST_START_R,         // P2 first serve of the game  (Power BLOCKED)
    ST_MOVE_LR,         // Ball flying left → right
    ST_MOVE_RL,         // Ball flying right → left
    ST_ZONE_L,          // Ball inside P1's hit zone   (Hit OR Power returns)
    ST_ZONE_R,          // Ball inside P2's hit zone   (Hit OR Power returns)
    ST_POINT_L,         // P1 just scored – score-dot blink
    ST_POINT_R,         // P2 just scored
    ST_RESUME_L,        // P1 re-serves after scoring  (Power BLOCKED)
    ST_RESUME_R,        // P2 re-serves after scoring  (Power BLOCKED)
    ST_WIN_L,           // P1 reached WIN_POINTS – game over
    ST_WIN_R,           // P2 reached WIN_POINTS – game over
    ST_FORFEIT_L,       // P1 forfeited (kept for compatibility)
    ST_FORFEIT_R,       // P2 forfeited
};

// ── Library objects ──────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, LED_PIN, NEO_GRB | NEO_KHZ800);
TM1637Display     disp1(TM1_CLK, TM1_DIO);   // P1 display (left)
TM1637Display     disp2(TM2_CLK, TM2_DIO);   // P2 display (right)

// ── Win jingle in flash: {freq Hz, duration ms} ──────────────
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

// ── Global game state ─────────────────────────────────────────
static uint32_t oldtime;
static uint8_t  thestate;

// Debounce: bstate = settled pin level; debtmr = settle countdown
static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

static uint16_t timer;         // General FSM countdown (ms)
static uint16_t timeout;       // 20s idle timeout for first serve
static uint16_t tonetimer;     // Remaining ms of current buzzer note
static uint16_t lockout_l;     // P1 anti-mash countdown
static uint16_t lockout_r;     // P2 anti-mash countdown

static uint8_t  ballblinkstate;
static uint8_t  pointblinkcount;
static uint8_t  ballpos;
static uint16_t speed;
static uint8_t  speedup;
static uint8_t  points_l;
static uint8_t  points_r;
static uint8_t  zone_l, zone_r;
static uint8_t  boost_l, boost_r;
static uint8_t  boosted;
static uint8_t  tonecount;
static uint8_t  tuneidx;
static uint8_t  aw_state;

// Idle animation sub-state
static uint16_t ai_h;
static uint8_t  ai_state;
static uint8_t  ai_pos;
static uint8_t  ai_brightness;

// BOTH BUTTON HOLD FORFEIT LOGIC (10 sec)
static uint16_t hold_l = 0;
static uint16_t hold_r = 0;

// ============================================================
//  SECTION 1 – SOUND FUNCTIONS
// ============================================================

/*
 * set_tone(freq, duration)
 *   Non-blocking tone start.  freq=0 → silence.
 *   tonetimer drives EV_TONETIMER to auto-stop the note later.
 */
static inline void set_tone(uint16_t freq, uint16_t duration) {
    tonetimer = duration;
    if (freq) tone(PIN_SOUND, freq);
    else      noTone(PIN_SOUND);
}

/*
 * tune_next()
 *   Advances the win jingle one note.
 *   Called each time EV_TONETIMER fires in WIN / FORFEIT states.
 */
static inline void tune_next() {
    if (tuneidx < NELEM(tune_win)) {
        uint16_t f = pgm_read_word(&tune_win[tuneidx].freq);
        uint16_t d = pgm_read_word(&tune_win[tuneidx].dur);
        set_tone(f, d);
        tuneidx++;
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
//  SECTION 2 – DISPLAY HELPERS  (dual TM1637)
// ============================================================

// Show "----" on a single display
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
 *   Shows score right-justified: "  0X"  (leading zero for single digits)
 *   Positions 0–1 blank; positions 2–3 are tens/units digits.
 */
static void disp_score_on(TM1637Display &d, uint8_t score) {
    uint8_t seg[4] = {
        SEG_BLNK,
        SEG_BLNK,
        d.encodeDigit(score / 10),   // tens (0 for scores < 10)
        d.encodeDigit(score % 10)    // units
    };
    d.setSegments(seg, 4, 0);
}

// Refresh BOTH displays with the current live scores
static void disp_scores() {
    disp_score_on(disp1, points_l);   // P1 left  display
    disp_score_on(disp2, points_r);   // P2 right display
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
//  SECTION 3 – BUTTON HELPERS
// ============================================================

/*
 * button_is_down(pin)
 *   Direct digitalRead check – returns 1 if pin is physically LOW now.
 *
 *   WHY raw read instead of debounced bstate:
 *   When Hit and Power are pressed within the same millisecond, Power's
 *   debounce timer is still counting when Hit fires its event.  At that
 *   moment bstate_lp/rp still reads HIGH (not-pressed), so a bstate
 *   check would miss the simultaneous hold.  A raw LOW on the pin is
 *   sufficient evidence that the player IS holding the button.
 *   We are not generating an edge event here – just sampling a hold –
 *   so a brief bounce glitch at most causes the boost to miss one frame,
 *   which is imperceptible.
 */
static inline uint8_t button_is_down(uint8_t pin) {
    return (digitalRead(pin) == LOW);   // INPUT_PULLUP: LOW = pressed
}

/*
 * do_debounce(tdiff, bstate, debtmr, pin, ev)
 *   Fires ev ONCE on falling edge (HIGH→LOW = button press).
 *   Holds the event silent while the settle window counts down.
 */
static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
    if (!*debtmr) {
        uint8_t state = digitalRead(pin);
        if (state != *bstate) {
            *debtmr = TIME_DEBOUNCE;
            if (!(*bstate = state)) return ev;  // HIGH→LOW = press
        }
    } else {
        if (*debtmr >= tdiff) *debtmr -= tdiff;
        else                  *debtmr = 0;
    }
    return 0;
}

/*
 * do_timer(tdiff, tmr, ev)
 *   Counts *tmr down to zero; fires ev exactly once when it reaches 0.
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
//  SECTION 4 – POWER SERVE HELPER
// ============================================================

/*
 * power_serve_speed()
 *   Returns a random ms/step value in [POWER_SERVE_MIN, POWER_SERVE_MAX].
 *   Used for Power serve AND standalone Power-hit return.
 *   Wide spread keeps the opponent guessing – cannot read timing from LEDs.
 */
static uint16_t power_serve_speed() {
    uint16_t range = POWER_SERVE_MAX - POWER_SERVE_MIN + 1;
    uint16_t s = POWER_SERVE_MIN + (uint16_t)random(range);
    Serial.print(F("[POWER] random speed chosen = "));
    Serial.print(s);
    Serial.println(F(" ms/step"));
    return s;
}


// ============================================================
//  SECTION 5 – LED DRAW HELPERS
// ============================================================

/*
 * draw_sides()
 *   Paint the cyan hit zones at both ends of the strip.
 *   zone_l / zone_r can shrink when a boost is used this rally.
 */
static void draw_sides() {
    for (uint8_t i = 0; i < zone_l; i++)
        one_d.setPixelColor(i, 0, 64, 64);                   // P1 zone: teal
    for (uint8_t i = 0; i < zone_r; i++)
        one_d.setPixelColor(NPIXELS - 1 - i, 0, 64, 64);     // P2 zone: teal
}

/*
 * draw_ball(dir, pos)
 *   Yellow ball at pos with a 5-LED diminishing tail.
 *   dir = +1  → tail trails leftward  (ball going right)
 *   dir = -1  → tail trails rightward (ball going left)
 */
static void draw_ball(int8_t dir, uint8_t pos) {
    uint8_t c = 255;
    for (uint8_t i = 0; i < 5 && pos < NPIXELS; i++) {
        one_d.setPixelColor(pos, c, c, 0);  // yellow, halved per step
        c >>= 1;
        pos -= dir;   // tail goes opposite to travel direction
    }
}

/*
 * draw_course(v)
 *   Clear strip → paint zones → paint score bars.
 *   P1 score: red  dots grow leftward  from centre.
 *   P2 score: green dots grow rightward from centre.
 *   Each point lights LEDS_PER_PT LEDs (auto-scales with NPIXELS/WIN_POINTS).
 *   v = SHOW_LO during flight, SHOW_HI at rest, 0 to hide bars entirely.
 */
static void draw_course(uint8_t v) {
    one_d.clear();
    draw_sides();
    if (v) {
        for (uint8_t i = 0; i < points_l; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++)
                one_d.setPixelColor(NPIXELS / 2 - 1 - (i * LEDS_PER_PT + j), v, 0, 0);
        for (uint8_t i = 0; i < points_r; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++)
                one_d.setPixelColor(NPIXELS / 2 + (i * LEDS_PER_PT + j), 0, v, 0);
    }
}


// ============================================================
//  SECTION 6 – IDLE ANIMATION
// ============================================================

static void animate_idle_init() {
    ai_h = 0; ai_state = 0; ai_pos = 0; ai_brightness = 0;
    Serial.println(F("[IDLE] animation reset → fade-in"));
}

static void draw_rainbow(uint8_t brightness) {
    for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, brightness));
    }
}

/*
 * animate_idle()
 *   Called every TIME_IDLE ms (≈25 fps) while in ST_IDLE.
 *   Phases: 0=fade-in, 1-3=full rainbow, 4-7=demo ball, 8-11=score fill, 12=fade-out → loops.
 */
static void animate_idle() {
    switch (ai_state) {
        case 0:  // fade-in 0 → 128
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness < 124) { ai_brightness += 4; }
            else { ai_brightness = 128; ai_state = 1;
                   Serial.println(F("[IDLE] fade-in done")); }
            break;
        case 1: case 2: case 3:  // full rainbow × 3 hue cycles
            draw_rainbow(128);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) { ai_h -= H_STEPS; ai_pos = 0; ai_state++; }
            break;
        case 4: case 6:  // demo ball L → R
            draw_course(0); draw_ball(+1, ai_pos++);
            if (ai_pos >= NPIXELS) ai_state++;
            break;
        case 5: case 7:  // demo ball R → L
            draw_course(0); draw_ball(-1, --ai_pos);
            if (!ai_pos) ai_state++;
            break;
        case 8: case 10:  // score fill expanding outward
            draw_course(0);
            for (uint8_t i = 0; i < ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;
        case 9: case 11:  // score fill collapsing inward
            draw_course(0);
            for (uint8_t i = 0; i < NPIXELS / 2 - ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;
        case 12:  // fade-out 128 → 0 then restart
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
//  SECTION 7 – WIN ANIMATION
// ============================================================

static void animate_win_init() { aw_state = 0; }

/*
 * animate_win(side)
 *   Multi-phase wipe on the winner's half of the strip.
 *   side=0 → P1 (red, left half)    side=1 → P2 (green, right half)
 *   Returns 1 while running; 0 when done → transition to ST_IDLE.
 */
static uint8_t animate_win(uint8_t side) {
    uint32_t clr;
    uint8_t  pos;
    if (side) { clr = Adafruit_NeoPixel::Color(0, 255, 0); pos = NPIXELS / 2; }
    else      { clr = Adafruit_NeoPixel::Color(255, 0, 0); pos = 0; }
    one_d.clear();
    if      (aw_state <  20) { if (aw_state & 1) for (uint8_t i=0;i<NPIXELS/2;i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state <  50) { for (uint8_t i=0;             i<aw_state-20;  i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state <  80) { for (uint8_t i=aw_state-50;   i<NPIXELS/2;   i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state < 110) { for (uint8_t i=0;             i<aw_state-80;  i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    else if (aw_state < 140) { for (uint8_t i=aw_state-110;  i<NPIXELS/2;   i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    one_d.show();
    return ++aw_state < 140;
}


// ============================================================
//  SECTION 8 – STATE MACHINE HELPERS
// ============================================================

/*
 * is_game_state(s)
 *   Returns 1 for states where the anti-mash lockout gate is active.
 *   ZONE states are intentionally EXCLUDED so there is zero lockout
 *   during a rally – Hit AND Power are fully live at all times.
 */
static uint8_t is_game_state(uint8_t s) {
    switch (s) {
        case ST_MOVE_LR: case ST_MOVE_RL:
        case ST_POINT_L: case ST_POINT_R:
        case ST_WIN_L:   case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            return 1;
        default:
            return 0;
    }
}

/*
 * speed_to_timer()
 *   Loads `timer` with the current ball step interval.
 *   Boosted ball travels at speed * BOOST_NUM / BOOST_DEN.
 *   Floor is 2 ms to prevent zero-timer lockup.
 */
static inline void speed_to_timer() {
    timer = boosted ? (uint16_t)speed * BOOST_NUM / BOOST_DEN : speed;
    if (timer < 2) timer = 2;
}


// ============================================================
//  SECTION 9 – SERVE HELPERS
//  All serve paths funnel through here so START and RESUME are
//  identical.  power=0 → fixed speed;  power=1 → random speed.
// ============================================================

/*
 * do_serve_L(power)
 *   Launch ball left → right (P1 serves toward P2 side).
 *   power=0  Normal serve: uses the speed already set by EXIT action.
 *   power=1  Power  serve: overrides speed with a random value.
 */
static void do_serve_L(uint8_t power) {
    if (power) {
        speed = power_serve_speed();
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1  → POWER SERVE  (random speed)"));
    } else {
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1  → normal serve"));
    }
    boosted = 0;
    set_state(ST_MOVE_LR);
}

/*
 * do_serve_R(power)
 *   Launch ball right → left (P2 serves toward P1 side).
 */
static void do_serve_R(uint8_t power) {
    if (power) {
        speed = power_serve_speed();
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2  → POWER SERVE  (random speed)"));
    } else {
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2  → normal serve"));
    }
    boosted = 0;
    set_state(ST_MOVE_RL);
}


// ============================================================
//  SECTION 10 – STATE TRANSITIONS  (set_state)
// ============================================================

/*
 * set_state(newstate)
 *   Runs EXIT actions for thestate, updates thestate, then runs
 *   ENTRY actions for newstate.
 *   All LED / display / sound / variable setup triggered by a state
 *   change lives here – never scattered around the loop() body.
 */
static void set_state(uint8_t newstate) {

    // Log every transition once
    Serial.print(F("[STATE] "));
    Serial.print(thestate);
    Serial.print(F(" → "));
    Serial.println(newstate);

    // ─────────────────────────────────────────────────────────
    // EXIT ACTIONS
    // ─────────────────────────────────────────────────────────
    switch (thestate) {

        // Entering a new game from idle/win/forfeit: full reset
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

        // P1 side about to serve: ball starts at pixel 0
        case ST_START_L:
        case ST_POINT_L:
        case ST_RESUME_L:
            ballpos = 0;
            speed   = TIME_SPEED_MIN + SERVE_DIST * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P2 side about to serve: ball starts at last pixel
        case ST_START_R:
        case ST_POINT_R:
        case ST_RESUME_R:
            ballpos = NPIXELS - 1;
            speed   = TIME_SPEED_MIN + SERVE_DIST * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        // P1 just returned: speed based on how far from wall they hit
        case ST_ZONE_L:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;   // caller sets boosted before set_state if needed
            Serial.print(F("[SPEED] P1 return base speed = "));
            Serial.print(speed);
            Serial.println(F(" ms/step  (boost applied in loop if flagged)"));
            break;

        // P2 just returned: same logic from the right wall
        case ST_ZONE_R:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS - 1 - ballpos);
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] P2 return base speed = "));
            Serial.print(speed);
            Serial.println(F(" ms/step  (boost applied in loop if flagged)"));
            break;
    }

    thestate = newstate;

    // ─────────────────────────────────────────────────────────
    // ENTRY ACTIONS
    // ─────────────────────────────────────────────────────────
    switch (thestate) {

        // ── Idle: rainbow + "----" on displays ───────────────
        case ST_IDLE:
            boost_l = boost_r = 0;
            zone_l  = zone_r  = ZONE_SIZE;
            animate_idle_init();
            timer = TIME_IDLE;
            disp_idle_both();
            Serial.println(F("[IDLE] rainbow playing – Hit or Power starts a game"));
            break;

        // ── First serve (Hit = normal  |  Power = BLOCKED) ───
        case ST_START_L:
        case ST_START_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = TIME_START_TIMEOUT;
            ballblinkstate = 0;
            ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;
            disp_scores();
            Serial.print(thestate == ST_START_L ? F("[START] P1") : F("[START] P2"));
            Serial.println(F(" – Hit=normal serve  (Power BLOCKED here)  20s timeout active"));
            break;

        // ── Ball in flight ────────────────────────────────────
        case ST_MOVE_LR:
        case ST_MOVE_RL:
            speed_to_timer();
            tonecount = TONE_INTERVAL;
            disp_scores();
            Serial.print(F("[BALL] dir="));
            Serial.print(thestate == ST_MOVE_LR ? F("L→R") : F("R→L"));
            Serial.print(F("  speed="));
            Serial.print(speed);
            Serial.print(F(" ms/step"));
            if (boosted) Serial.print(F("  [BOOSTED]"));
            Serial.println();
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
            lockout_l = lockout_r = TIME_LOCKOUT;
            disp_scores();
            Serial.print(F("[SCORE] P1="));
            Serial.print(points_l);
            Serial.print(F("  P2="));
            Serial.print(points_r);
            Serial.print(F("  (first to "));
            Serial.print(WIN_POINTS);
            Serial.println(F(" wins)"));
            break;

        // ── Re-serve screen (Hit only – Power BLOCKED once) ───
        case ST_RESUME_L:
        case ST_RESUME_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = 0;   // no auto-fire; scorer must press Hit
            ballblinkstate = 0;
            disp_scores();
            Serial.print(thestate == ST_RESUME_L ? F("[WAIT] P1") : F("[WAIT] P2"));
            Serial.println(F(" – Hit to re-serve  (Power BLOCKED on first re-serve)"));
            break;

        // ── P1 wins ───────────────────────────────────────────
        case ST_WIN_L:
            disp_win_msg(disp1);   // "WOn "
            disp_dead_msg(disp2);  // "dEAd"
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[WIN] *** P1 WINS ***   P1:'WOn'  P2:'dEAd'"));
            Serial.print(F("[WIN] Final score  P1="));
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
            Serial.println(F("[WIN] *** P2 WINS ***   P1:'dEAd'  P2:'WOn'"));
            Serial.print(F("[WIN] Final score  P1="));
            Serial.print(points_l);
            Serial.print(F("  P2="));
            Serial.println(points_r);
            break;

        // ── Forfeit states (kept for compatibility) ───────────
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
    Serial.println(F("========================================"));
    Serial.println(F("   1D PONG  v1.7  –  POWER HIT FIX"));
    Serial.println(F("========================================"));
    Serial.print(F("   NPIXELS       = ")); Serial.println(NPIXELS);
    Serial.print(F("   ZONE_SIZE     = ")); Serial.println(ZONE_SIZE);
    Serial.print(F("   WIN_POINTS    = ")); Serial.println(WIN_POINTS);
    Serial.print(F("   LEDS_PER_PT   = ")); Serial.println(LEDS_PER_PT);
    Serial.print(F("   SPEED_MIN     = ")); Serial.print(TIME_SPEED_MIN);  Serial.println(F(" ms"));
    Serial.print(F("   POWER range   = ")); Serial.print(POWER_SERVE_MIN);
    Serial.print(F(" – "));                 Serial.print(POWER_SERVE_MAX); Serial.println(F(" ms (random)"));
    Serial.println(F("----------------------------------------"));
    Serial.println(F("   SERVE  rules:"));
    Serial.println(F("     ST_START  – Hit=normal  Power=BLOCKED"));
    Serial.println(F("     ST_RESUME – Hit=normal  Power=BLOCKED"));
    Serial.println(F("   RALLY  rules:"));
    Serial.println(F("     Hit alone       → normal speed return"));
    Serial.println(F("     Power alone     → RANDOM speed return  ← FIXED"));
    Serial.println(F("     Hit + Power held→ BOOST (×75%, zone–1)"));
    Serial.println(F("========================================"));

    randomSeed(analogRead(A0));   // seed RNG from floating ADC pin

    // Pull all unused port pins HIGH to suppress floating-input noise
    PORTB = PORTC = PORTD = 0xFF;

    // Button inputs: HIGH at rest (INPUT_PULLUP), LOW when pressed
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
    disp_idle_both();   // "----" on both immediately

    // WS2812B strip – all off
    one_d.begin();
    one_d.show();

    // Boot into idle
    thestate = ST_IDLE;
    set_state(ST_IDLE);

    Serial.println(F("   Ready!  Press any button to start."));
    Serial.println(F("========================================"));
}


// ============================================================
//  MAIN LOOP
// ============================================================

#define chk_ev(ev)  (events & (ev))

void loop() {
    uint32_t now;
    uint8_t tdiff  = (now = millis()) - oldtime;
    uint8_t events = 0;

    // ── Sample debouncers and tick all timers once per ms ────
    if (tdiff) {
        oldtime = now;

        // Debounce every button: fire one event per falling edge (press)
        events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, P1_HIT,   EV_BUT_LS_PRESS);
        events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, P2_HIT,   EV_BUT_RS_PRESS);
        events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, P1_POWER, EV_BUT_LP_PRESS);
        events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, P2_POWER, EV_BUT_RP_PRESS);

        // Timers
        events |= do_timer(tdiff, &timer,     EV_TIMER);
        events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
        events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);

        // Lockout just counts down – no event flag needed
        do_timer(tdiff, &lockout_l, 0);
        do_timer(tdiff, &lockout_r, 0);
    }

    // ── Log every button edge ONCE (before any state consumes it) ──
    if (chk_ev(EV_BUT_LS_PRESS)) Serial.println(F("[BTN] P1 Hit   ↓"));
    if (chk_ev(EV_BUT_RS_PRESS)) Serial.println(F("[BTN] P2 Hit   ↓"));
    if (chk_ev(EV_BUT_LP_PRESS)) Serial.println(F("[BTN] P1 Power ↓"));
    if (chk_ev(EV_BUT_RP_PRESS)) Serial.println(F("[BTN] P2 Power ↓"));

    // ── Lockout gate: suppress Hit events during flight/score/end ──
    //    ZONE states are NOT in is_game_state() → no suppression during rally.
    if (is_game_state(thestate)) {
        if (lockout_l) events &= ~EV_BUT_LS_PRESS;
        if (lockout_r) events &= ~EV_BUT_RS_PRESS;
    }
    // Re-arm lockout whenever a Hit press passes through the gate
    if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
    if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;


    // ─────────────────────────────────────────────────────────
    //  STATE MACHINE
    // ─────────────────────────────────────────────────────────
    switch (thestate) {

        // ── IDLE: rainbow plays; Hit or Power starts game ─────
        case ST_IDLE:
            if      (chk_ev(EV_BUT_LS_PRESS) || chk_ev(EV_BUT_LP_PRESS)) { set_state(ST_START_L); }
            else if (chk_ev(EV_BUT_RS_PRESS) || chk_ev(EV_BUT_RP_PRESS)) { set_state(ST_START_R); }
            else if (chk_ev(EV_TIMER)) { timer = TIME_IDLE; animate_idle(); }
            break;

        // ── START_L: P1's FIRST serve of the game ─────────────
        //    Hit   → normal serve  |  Power → BLOCKED (ignored)
        case ST_START_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);   // normal serve
            } else if (chk_ev(EV_BUT_LP_PRESS)) {
                // Power blocked on first serve – log it so the player knows
                Serial.println(F("[BLOCKED] P1 Power – not allowed on first serve"));
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

        // ── START_R: P2's FIRST serve ─────────────────────────
        //    Hit   → normal serve  |  Power → BLOCKED (ignored)
        case ST_START_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                do_serve_R(0);   // normal serve
            } else if (chk_ev(EV_BUT_RP_PRESS)) {
                Serial.println(F("[BLOCKED] P2 Power – not allowed on first serve"));
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

        // ── MOVE_LR: ball flying left → right ─────────────────
        case ST_MOVE_LR:
            if (chk_ev(EV_TIMER)) {
                if (!--tonecount) {
                    set_tone(SND_TICK, TIME_TONE_MOVE);
                    tonecount = TONE_INTERVAL;
                }
                speed_to_timer();
                draw_course(SHOW_LO);
                draw_ball(+1, ballpos);
                one_d.show();
                ballpos++;
                if (NPIXELS - 1 - ballpos < zone_r) {
                    Serial.print(F("[BALL] entering P2 zone  pos="));
                    Serial.println(ballpos);
                    set_state(ST_ZONE_R);
                }
            }
            break;

        // ── MOVE_RL: ball flying right → left ─────────────────
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
                if (ballpos < zone_l) {
                    Serial.print(F("[BALL] entering P1 zone  pos="));
                    Serial.println(ballpos);
                    set_state(ST_ZONE_L);
                }
            }
            break;

        // ── ZONE_L: ball inside P1's hit zone ─────────────────
        //
        //  ╔══════════════════════════════════════════════════════╗
        //  ║  THREE WAYS P1 CAN RETURN THE BALL HERE:            ║
        //  ║                                                      ║
        //  ║  1) Power alone  → random speed (no boost)          ║
        //  ║     Player pressed Power but NOT Hit.               ║
        //  ║     Speed is re-rolled randomly (fun / unpredictable)║
        //  ║                                                      ║
        //  ║  2) Hit alone    → normal computed speed (no boost)  ║
        //  ║     Player pressed Hit, Power is NOT held.          ║
        //  ║                                                      ║
        //  ║  3) Hit + Power  → BOOST  (zone shrinks, faster)    ║
        //  ║     Player pressed Hit while Power pin is LOW.      ║
        //  ║     speed×BOOST_NUM/BOOST_DEN,  zone_l shrinks 1.   ║
        //  ║                                                      ║
        //  ║  Power detection uses raw pin read OR event flag    ║
        //  ║  because both buttons may land in the same ms tick. ║
        //  ╚══════════════════════════════════════════════════════╝
        //
        //  Ball only actually scores when it reaches pixel 0 (the wall).
        //
        case ST_ZONE_L:
        {
            // Was Power pressed this tick OR is it currently held down?
            uint8_t power_active = chk_ev(EV_BUT_LP_PRESS) || button_is_down(P1_POWER);

            if (chk_ev(EV_BUT_LP_PRESS) && !chk_ev(EV_BUT_LS_PRESS)) {
                // ── Case 1: POWER ALONE → random speed return ──────────
                Serial.print(F("[HIT] P1 POWER RETURN at pos="));
                Serial.println(ballpos);
                set_tone(SND_POWER_HIT, TIME_TONE_BOUNCE);

                // Override the speed that EXIT will compute with a random value.
                // We set speed directly here; EXIT of ST_ZONE_L will overwrite
                // it with the distance formula, but we call set_state AFTER we
                // override, so the ENTRY of ST_MOVE_LR picks up our value.
                // Actually: EXIT runs *inside* set_state – we must set speed
                // AFTER calling it, then fix up speed_to_timer manually.
                boosted = 0;
                set_state(ST_MOVE_LR);        // EXIT sets speed via formula
                speed = power_serve_speed();   // override with random speed
                speed_to_timer();              // reload timer with new speed
                Serial.println(F("[POWER-HIT] P1 random speed applied after state transition"));

            } else if (chk_ev(EV_BUT_LS_PRESS)) {
                // ── Case 2 / 3: HIT (with or without boost) ────────────
                Serial.print(F("[HIT] P1 Hit at pos="));
                Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);

                if (power_active && zone_l > 1) {
                    // Case 3: Hit + Power → BOOST
                    zone_l--;   // shrink zone permanently for this rally
                    boosted = 1;
                    boost_l++;
                    Serial.print(F("[BOOST] P1 boost!  zone_l="));
                    Serial.println(zone_l);
                } else {
                    // Case 2: plain Hit
                    boosted = 0;
                }
                // boosted must be set BEFORE set_state so ENTRY's
                // speed_to_timer() picks up the correct value.
                set_state(ST_MOVE_LR);

            } else if (chk_ev(EV_TIMER)) {
                // ── Ball advances toward wall; check for miss ──────────
                if (!ballpos) {
                    Serial.println(F("[MISS] P1 missed – P2 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_r >= WIN_POINTS) set_state(ST_WIN_R);
                    else                          set_state(ST_POINT_R);
                } else {
                    speed_to_timer();
                    ballpos--;
                    draw_course(SHOW_LO);
                    draw_ball(-1, ballpos);
                    one_d.show();
                }
            }
            break;
        }

        // ── ZONE_R: ball inside P2's hit zone ─────────────────
        //
        //  Same three-case logic as ZONE_L, mirrored for P2.
        //
        //  Case 1: Power alone  → random speed return
        //  Case 2: Hit alone    → normal computed speed
        //  Case 3: Hit + Power  → BOOST  (zone shrinks, faster)
        //
        case ST_ZONE_R:
        {
            uint8_t power_active = chk_ev(EV_BUT_RP_PRESS) || button_is_down(P2_POWER);

            if (chk_ev(EV_BUT_RP_PRESS) && !chk_ev(EV_BUT_RS_PRESS)) {
                // ── Case 1: POWER ALONE → random speed return ──────────
                Serial.print(F("[HIT] P2 POWER RETURN at pos="));
                Serial.println(ballpos);
                set_tone(SND_POWER_HIT, TIME_TONE_BOUNCE);

                boosted = 0;
                set_state(ST_MOVE_RL);        // EXIT sets speed via formula
                speed = power_serve_speed();   // override with random speed
                speed_to_timer();              // reload timer with new speed
                Serial.println(F("[POWER-HIT] P2 random speed applied after state transition"));

            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                // ── Case 2 / 3: HIT (with or without boost) ────────────
                Serial.print(F("[HIT] P2 Hit at pos="));
                Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);

                if (power_active && zone_r > 1) {
                    // Case 3: Hit + Power → BOOST
                    zone_r--;
                    boosted = 1;
                    boost_r++;
                    Serial.print(F("[BOOST] P2 boost!  zone_r="));
                    Serial.println(zone_r);
                } else {
                    // Case 2: plain Hit
                    boosted = 0;
                }
                set_state(ST_MOVE_RL);

            } else if (chk_ev(EV_TIMER)) {
                // ── Ball advances toward wall; check for miss ──────────
                if (ballpos == NPIXELS - 1) {
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
        }

        // ── POINT_L: P1 scored – blink new score dot ──────────
        //    P1 can skip the blink by pressing Hit early.
        case ST_POINT_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                set_state(ST_RESUME_L);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
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

        // ── RESUME_L: P1 scored; P1 re-serves with Hit ────────
        //    Power is INTENTIONALLY IGNORED here (blocked once per point).
        //    First return after a point is always a plain normal serve.
        case ST_RESUME_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);   // always normal speed on re-serve
            } else if (chk_ev(EV_BUT_LP_PRESS)) {
                // Silently blocked – player may not know, log it for debug
                Serial.println(F("[BLOCKED] P1 Power – not allowed on re-serve"));
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

        // ── RESUME_R: P2 scored; P2 re-serves with Hit ────────
        case ST_RESUME_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                do_serve_R(0);   // always normal speed on re-serve
            } else if (chk_ev(EV_BUT_RP_PRESS)) {
                Serial.println(F("[BLOCKED] P2 Power – not allowed on re-serve"));
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

        // ── WIN & FORFEIT: jingle + animation ─────────────────
        case ST_WIN_L: case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            if (chk_ev(EV_TONETIMER)) {
                events &= ~EV_TONETIMER;   // consume so global silencer below doesn't kill jingle
                tune_next();
            }
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[RESTART] P1 Hit → new game"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[RESTART] P2 Hit → new game"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_WIN_BLINK;
                uint8_t p2_side = (thestate == ST_WIN_R || thestate == ST_FORFEIT_L);
                if (!animate_win(p2_side)) {
                    Serial.println(F("[WIN] animation complete → idle"));
                    set_state(ST_IDLE);
                }
            }
            break;

        default:
            Serial.println(F("[ERROR] unknown state – resetting to idle"));
            set_state(ST_IDLE);
            break;
    }

    // ── Global tone-off ──────────────────────────────────────
    //    If EV_TONETIMER was not consumed by the WIN/FORFEIT handler
    //    above, silence the buzzer – the note has run its full duration.
    if (chk_ev(EV_TONETIMER))
        set_tone(0, 0);
    // ============================================================
    // BOTH BUTTON HOLD FORFEIT LOGIC (10 sec)
    // ============================================================

    bool p1_hold = (digitalRead(P1_HIT) == LOW && digitalRead(P1_POWER) == LOW);
    bool p2_hold = (digitalRead(P2_HIT) == LOW && digitalRead(P2_POWER) == LOW);

    // P1 hold tracking
    if (p1_hold) {
        hold_l += tdiff;
        if (hold_l >= 10000) {
            Serial.println(F("[FORFEIT] P1 held both buttons 10s"));
            set_state(ST_FORFEIT_L);
            hold_l = 0;
        }
    } else {
        hold_l = 0;
    }

    // P2 hold tracking
    if (p2_hold) {
        hold_r += tdiff;
        if (hold_r >= 10000) {
            Serial.println(F("[FORFEIT] P2 held both buttons 10s"));
            set_state(ST_FORFEIT_R);
            hold_r = 0;
        }
    } else {
        hold_r = 0;
    }
}

// vim: syn=cpp
