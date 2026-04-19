/*
 * ============================================================
 *  1D LED PONG  v1.5  –  Arduino Uno
 * ============================================================
 *  Based on 1D Pong v1.4
 *
 *  CHANGES IN v2.1:
 *   1. Any button (Hit OR Power) starts the game from idle.
 *      The first serve is always a normal-speed ball regardless
 *      of which button was pressed — Power only takes effect
 *      mid-game after the initial serve.
 *   2. Power button is now accepted as a valid serve trigger in
 *      ST_START_L, ST_START_R, ST_RESUME_L, ST_RESUME_R.
 *      Previously only EV_BUT_LS/RS_PRESS was checked, so a
 *      Power-press during the serve blink was silently ignored.
 *   3. Power serve is "human-level unpredictable": speed is
 *      randomised in the range [TIME_SPEED_MIN .. speed_normal]
 *      rather than the deterministic wall-proximity formula.
 *      This simulates an erratic, hard-to-read human throw.
 *
 *  LIBRARIES (install via Library Manager):
 *    Adafruit NeoPixel
 *    TM1637Display  (by avishorp)
 *
 *  WIRING: (unchanged from v2.0)
 *    Pin  2  → WS2812B DIN  (330Ω series resistor recommended)
 *    Pin  3  → Passive buzzer
 *    Pin  4  → P1 Hit   button → GND   (INPUT_PULLUP, active LOW)
 *    Pin  5  → P1 Power button → GND
 *    Pin  6  → P2 Hit   button → GND
 *    Pin  7  → P2 Power button → GND
 *    Pin  8  → TM1637 #1 CLK  (P1 display)
 *    Pin  9  → TM1637 #1 DIO
 *    Pin 10  → TM1637 #2 CLK  (P2 display)
 *    Pin 11  → TM1637 #2 DIO
 * ============================================================
 */

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>

#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// ── Pin assignments ──────────────────────────────────────────
#define LED_PIN   2
#define BUZZER    3
#define P1_HIT    4
#define P1_POWER  5
#define P2_HIT    6
#define P2_POWER  7
#define TM1_CLK   8
#define TM1_DIO   9
#define TM2_CLK  10
#define TM2_DIO  11

#define PIN_WSDATA   LED_PIN
#define PIN_SOUND    BUZZER
#define PIN_BUT_LS   P1_HIT
#define PIN_BUT_LP   P1_POWER
#define PIN_BUT_RS   P2_HIT
#define PIN_BUT_RP   P2_POWER

// ── Game parameters ─────────────────────────────────────────
#define NPIXELS          60
#define ZONE_SIZE         7
#define SHOW_LO          12
#define SHOW_HI          48
#define WIN_POINTS        9
#define TONE_INTERVAL     5

// ── Dynamic score bar ────────────────────────────────────────
#define SCORE_PIXELS  (NPIXELS / 2 - ZONE_SIZE)
#define LEDS_PER_PT   (SCORE_PIXELS / WIN_POINTS)

// ── Power serve speed band ───────────────────────────────────
//  Normal serve uses the standard formula.
//  Power serve picks a random speed in this window — fast and
//  unpredictable, like a human throwing with variable force.
#define POWER_SERVE_MIN   TIME_SPEED_MIN          // absolute floor
#define POWER_SERVE_MAX   (TIME_SPEED_MIN + 8 * TIME_SPEED_INTERVAL)  // upper cap

// ── Event flags ──────────────────────────────────────────────
#define EV_BUT_LS_PRESS  0x01
#define EV_BUT_RS_PRESS  0x02
#define EV_BUT_LP_PRESS  0x04
#define EV_BUT_RP_PRESS  0x08
#define EV_TIMER         0x10
#define EV_TIMEOUT       0x20
#define EV_TONETIMER     0x40

// ── Timing constants ─────────────────────────────────────────
#define TIME_DEBOUNCE        8
#define TIME_IDLE           40
#define TIME_START_TIMEOUT  20000
#define TIME_BALL_BLINK     150
#define TIME_SPEED_MIN       10
#define TIME_SPEED_INTERVAL   3
#define TIME_POINT_BLINK    233
#define TIME_WIN_BLINK       85
#define TIME_LOCKOUT        250
#define TIME_TONE_BOUNCE     50
#define TIME_TONE_MOVE       25
#define TIME_TONE_SCORE      50
#define TIME_TONE_SERVE      50
#define TIME_FORFEIT      10000

// ── 7-segment letters ────────────────────────────────────────
#define SEG_DASH  0x40
#define SEG_BLNK  0x00
#define SEG_A     0x77
#define SEG_d     0x5E
#define SEG_E     0x79
#define SEG_H     0x76
#define SEG_n     0x54
#define SEG_O     0x3F
#define SEG_S     0x6D
#define SEG_U     0x3E

// ── Game states ─────────────────────────────────────────────
enum {
    ST_IDLE = 0,
    ST_START_L, ST_START_R,
    ST_MOVE_LR, ST_MOVE_RL,
    ST_ZONE_L,  ST_ZONE_R,
    ST_POINT_L, ST_POINT_R,
    ST_RESUME_L,ST_RESUME_R,
    ST_WIN_L,   ST_WIN_R,
    ST_FORFEIT_L, ST_FORFEIT_R,
};

// ── Library objects ──────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, LED_PIN, NEO_GRB | NEO_KHZ800);
TM1637Display     disp1(TM1_CLK, TM1_DIO);
TM1637Display     disp2(TM2_CLK, TM2_DIO);

// ── Sound ────────────────────────────────────────────────────
#define SND_BOUNCE  196
#define SND_TICK    392
#define SND_SCORE   523
#define SND_SERVE   174
// Extra sound for a power serve — slightly higher pitch to hint at chaos
#define SND_POWER_SERVE 220

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

// ── Global game state ────────────────────────────────────────
static uint32_t oldtime;
static uint8_t  thestate;

static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

static uint16_t timer;
static uint16_t timeout;
static uint16_t tonetimer;
static uint16_t lockout_l;
static uint16_t lockout_r;

static uint16_t forfeit_hold_l;
static uint16_t forfeit_hold_r;

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

// ── Idle animation state ─────────────────────────────────────
static uint16_t ai_h;
static uint8_t  ai_state;
static uint8_t  ai_pos;
static uint8_t  ai_brightness;

#define H_STEPS  1542

// ─────────────────────────────────────────────────────────────
//  SOUND FUNCTIONS
// ─────────────────────────────────────────────────────────────

static inline void set_tone(uint16_t freq, uint16_t duration) {
    tonetimer = duration;
    if (freq) tone(PIN_SOUND, freq);
    else      noTone(PIN_SOUND);
}

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
//  DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────

static void disp_dashes(TM1637Display &d) {
    uint8_t seg[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
    d.setSegments(seg, 4, 0);
}

static void disp_idle_both() {
    disp_dashes(disp1);
    disp_dashes(disp2);
}

static void disp_score_on(TM1637Display &d, uint8_t score) {
    uint8_t seg[4] = {
        SEG_BLNK,
        SEG_BLNK,
        d.encodeDigit(score / 10),
        d.encodeDigit(score % 10)
    };
    d.setSegments(seg, 4, 0);
}

static void disp_scores() {
    disp_score_on(disp1, points_l);
    disp_score_on(disp2, points_r);
}

static void disp_win_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_U, SEG_O, SEG_n, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

static void disp_dead_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_d, SEG_E, SEG_A, SEG_d};
    d.setSegments(seg, 4, 0);
}

static void disp_kms_msg(TM1637Display &d) {
    uint8_t seg[4] = {SEG_H, SEG_n, SEG_S, SEG_BLNK};
    d.setSegments(seg, 4, 0);
}

// ─────────────────────────────────────────────────────────────
//  BUTTON HELPERS
// ─────────────────────────────────────────────────────────────

static inline uint8_t button_is_down(uint8_t pin) {
    switch (pin) {
        case PIN_BUT_LS: return !debtmr_ls && !bstate_ls;
        case PIN_BUT_RS: return !debtmr_rs && !bstate_rs;
        case PIN_BUT_LP: return !debtmr_lp && !bstate_lp;
        case PIN_BUT_RP: return !debtmr_rp && !bstate_rp;
    }
    return 0;
}

static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
    if (!*debtmr) {
        uint8_t state = digitalRead(pin);
        if (state != *bstate) {
            *debtmr = TIME_DEBOUNCE;
            if (!(*bstate = state)) return ev;
        }
    } else {
        if (*debtmr >= tdiff) *debtmr -= tdiff;
        else                  *debtmr = 0;
    }
    return 0;
}

static inline uint8_t do_timer(uint8_t tdiff, uint16_t *tmr, uint8_t ev) {
    if (*tmr) {
        if (*tmr >= tdiff) *tmr -= tdiff;
        else               *tmr = 0;
        if (!*tmr) return ev;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
//  POWER SERVE: randomised "human-level" speed
//
//  Returns a speed (ms per step) chosen randomly between
//  POWER_SERVE_MIN and POWER_SERVE_MAX, inclusive.
//  The randomness means the opponent cannot predict the timing
//  from serve position alone — it mimics a real human throw.
// ─────────────────────────────────────────────────────────────
static uint16_t power_serve_speed() {
    uint16_t range = POWER_SERVE_MAX - POWER_SERVE_MIN + 1;
    uint16_t s = POWER_SERVE_MIN + (random(range));
    Serial.print(F("[POWER-SERVE] random speed="));
    Serial.print(s);
    Serial.println(F("ms"));
    return s;
}

// ─────────────────────────────────────────────────────────────
//  LED DRAW HELPERS
// ─────────────────────────────────────────────────────────────

static void draw_sides() {
    for (uint8_t i = 0; i < zone_l; i++)
        one_d.setPixelColor(i, 0, 64, 64);
    for (uint8_t i = 0; i < zone_r; i++)
        one_d.setPixelColor(NPIXELS - 1 - i, 0, 64, 64);
}

static void draw_ball(int8_t dir, uint8_t pos) {
    uint8_t c = 255;
    for (uint8_t i = 0; i < 5 && pos < NPIXELS; i++) {
        one_d.setPixelColor(pos, c, c, 0);
        c >>= 1;
        pos -= dir;
    }
}

static void draw_course(uint8_t v) {
    one_d.clear();
    draw_sides();
    if (v) {
        for (uint8_t i = 0; i < points_l; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 - 1 - (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, v, 0, 0);
            }
        for (uint8_t i = 0; i < points_r; i++)
            for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                uint8_t px = NPIXELS / 2 + (i * LEDS_PER_PT + j);
                one_d.setPixelColor(px, 0, v, 0);
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

static void draw_rainbow(uint8_t brightness) {
    for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, brightness));
    }
}

static void animate_idle() {
    switch (ai_state) {
        case 0:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness < 124) { ai_brightness += 4; }
            else { ai_brightness = 128; ai_state = 1; Serial.println(F("[IDLE] fade-in complete")); }
            break;
        case 1: case 2: case 3:
            draw_rainbow(128);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) { ai_h -= H_STEPS; ai_pos = 0; ai_state++; }
            break;
        case 4: case 6:
            draw_course(0); draw_ball(+1, ai_pos++);
            if (ai_pos >= NPIXELS) ai_state++;
            break;
        case 5: case 7:
            draw_course(0); draw_ball(-1, --ai_pos);
            if (!ai_pos) ai_state++;
            break;
        case 8: case 10:
            draw_course(0);
            for (uint8_t i = 0; i < ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;
        case 9: case 11:
            draw_course(0);
            for (uint8_t i = 0; i < NPIXELS / 2 - ai_pos; i++) {
                one_d.setPixelColor(NPIXELS / 2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS / 2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS / 2) { ai_state++; ai_pos = 0; }
            break;
        case 12:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness > 4) { ai_brightness -= 4; }
            else { ai_brightness = 0; ai_state = 0; Serial.println(F("[IDLE] fade-out → looping")); }
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

static uint8_t animate_win(uint8_t side) {
    uint32_t clr;
    uint8_t  pos;
    if (side) { clr = Adafruit_NeoPixel::Color(0, 255, 0); pos = NPIXELS / 2; }
    else      { clr = Adafruit_NeoPixel::Color(255, 0, 0); pos = 0; }
    one_d.clear();
    if      (aw_state < 20)  { if (aw_state & 0x01) for (uint8_t i=0;i<NPIXELS/2;i++) one_d.setPixelColor(pos+i, clr); }
    else if (aw_state < 50)  { for (uint8_t i=0;          i<aw_state-20;    i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state < 80)  { for (uint8_t i=aw_state-50;i<NPIXELS/2;     i++) one_d.setPixelColor(pos+i,              clr); }
    else if (aw_state < 110) { for (uint8_t i=0;          i<aw_state-80;    i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    else if (aw_state < 140) { for (uint8_t i=aw_state-110;i<NPIXELS/2;    i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    one_d.show();
    return ++aw_state < 140;
}

// ─────────────────────────────────────────────────────────────
//  STATE MACHINE HELPERS
// ─────────────────────────────────────────────────────────────

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

static inline void speed_to_timer() {
    timer = boosted ? speed * 3 / 4 : speed;
    if (timer < 2) timer = 2;
}

// ─────────────────────────────────────────────────────────────
//  STATE TRANSITIONS
// ─────────────────────────────────────────────────────────────

static void set_state(uint8_t newstate) {
    Serial.print(F("[STATE] ")); Serial.print(thestate);
    Serial.print(F(" -> "));    Serial.println(newstate);

    // ── EXIT ACTIONS ─────────────────────────────────────────
    switch (thestate) {
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
        case ST_START_L:
        case ST_POINT_L:
        case ST_RESUME_L:
            ballpos = 0;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;
        case ST_START_R:
        case ST_POINT_R:
        case ST_RESUME_R:
            ballpos = NPIXELS - 1;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;
        case ST_ZONE_L:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] new speed=")); Serial.print(speed); Serial.println(F("ms"));
            break;
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
        case ST_IDLE:
            boost_l = boost_r = 0;
            zone_l  = zone_r  = ZONE_SIZE;
            animate_idle_init();
            timer = TIME_IDLE;
            disp_idle_both();
            Serial.println(F("[IDLE] awaiting player – rainbow – displays: ----"));
            break;

        case ST_START_L:
        case ST_START_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = TIME_START_TIMEOUT;
            ballblinkstate = 0;
            ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;
            disp_scores();
            Serial.print(F("[START] "));
            Serial.print(thestate == ST_START_L ? F("P1") : F("P2"));
            Serial.println(F(" – Hit=normal serve  Power=random serve  (20s timeout)"));
            break;

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

        case ST_POINT_L:
        case ST_POINT_R:
            pointblinkcount = 7;
            if (!boost_l && zone_l < ZONE_SIZE) zone_l++;
            if (!boost_r && zone_r < ZONE_SIZE) zone_r++;
            timer     = TIME_POINT_BLINK;
            if (boost_l) boost_l--;
            if (boost_r) boost_r--;
            lockout_l = lockout_r = TIME_LOCKOUT;
            disp_scores();
            Serial.print(F("[SCORE] P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));      Serial.print(points_r);
            Serial.print(F("  (need "));   Serial.print(WIN_POINTS);
            Serial.println(F(" to win)"));
            break;

        case ST_RESUME_L:
        case ST_RESUME_R:
            draw_course(SHOW_HI); one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = 0;
            ballblinkstate = 0;
            disp_scores();
            Serial.print(F("[WAIT] "));
            Serial.print(thestate == ST_RESUME_L ? F("P1") : F("P2"));
            Serial.println(F(" – Hit=normal serve  Power=random serve"));
            break;

        case ST_WIN_L:
            disp_win_msg(disp1);
            disp_dead_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[WIN] P1 wins!  disp1:'WOn '  disp2:'dEAd'"));
            break;

        case ST_WIN_R:
            disp_dead_msg(disp1);
            disp_win_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[WIN] P2 wins!  disp1:'dEAd'  disp2:'WOn '"));
            break;

        case ST_FORFEIT_L:
            disp_kms_msg(disp1);
            disp_win_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            forfeit_hold_l = forfeit_hold_r = 0;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[FORFEIT] P1 forfeited! P2 wins"));
            break;

        case ST_FORFEIT_R:
            disp_win_msg(disp1);
            disp_kms_msg(disp2);
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            forfeit_hold_l = forfeit_hold_r = 0;
            animate_win_init();
            timer = TIME_WIN_BLINK; tuneidx = 0; tune_next();
            Serial.println(F("[FORFEIT] P2 forfeited! P1 wins"));
            break;
    }
}

// ─────────────────────────────────────────────────────────────
//  SERVE HELPERS
//
//  do_serve_L / do_serve_R encapsulate all serve logic so it
//  can be called identically from ST_START and ST_RESUME.
//
//  Hit  button → normal fixed speed (deterministic, readable).
//  Power button → randomised speed  (unpredictable, "human").
//  Either button always launches the correct direction.
// ─────────────────────────────────────────────────────────────

static void do_serve_L(uint8_t power) {
    if (power) {
        speed = power_serve_speed();   // random speed
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1 POWER serve → unpredictable speed"));
    } else {
        // speed already set by EXIT action of START_L / RESUME_L
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P1 normal serve"));
    }
    boosted = 0;
    set_state(ST_MOVE_LR);
}

static void do_serve_R(uint8_t power) {
    if (power) {
        speed = power_serve_speed();
        set_tone(SND_POWER_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2 POWER serve → unpredictable speed"));
    } else {
        set_tone(SND_SERVE, TIME_TONE_SERVE);
        Serial.println(F("[SERVE] P2 normal serve"));
    }
    boosted = 0;
    set_state(ST_MOVE_RL);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    Serial.println(F("===================================="));
    Serial.println(F("  1D PONG  v2.1  –  STARTUP"));
    Serial.println(F("===================================="));
    Serial.print(F("  NPIXELS      = ")); Serial.println(NPIXELS);
    Serial.print(F("  WIN_POINTS   = ")); Serial.println(WIN_POINTS);
    Serial.print(F("  ZONE_SIZE    = ")); Serial.println(ZONE_SIZE);
    Serial.print(F("  POWER_SERVE  = ")); Serial.print(POWER_SERVE_MIN);
    Serial.print(F(" – ")); Serial.print(POWER_SERVE_MAX); Serial.println(F(" ms (random)"));
    Serial.print(F("  TIME_FORFEIT = ")); Serial.print(TIME_FORFEIT/1000); Serial.println(F("s"));

    randomSeed(analogRead(A0));   // seed RNG from floating ADC pin

    PORTB = PORTC = PORTD = 0xff;

    pinMode(P1_HIT,   INPUT_PULLUP);
    pinMode(P1_POWER, INPUT_PULLUP);
    pinMode(P2_HIT,   INPUT_PULLUP);
    pinMode(P2_POWER, INPUT_PULLUP);

    digitalWrite(BUZZER, LOW);
    pinMode(BUZZER, OUTPUT);

    disp1.setBrightness(5);
    disp2.setBrightness(5);
    disp_idle_both();

    one_d.begin();
    one_d.show();

    thestate = ST_IDLE;
    set_state(ST_IDLE);

    Serial.println(F("  ANY button starts the game from idle."));
    Serial.println(F("  First serve: Hit = normal | Power = random speed"));
    Serial.println(F("  Mid-rally:   Power + Hit = speed boost (zone shrinks)"));
    Serial.println(F("  Hold Hit+Power 10s to forfeit"));
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

    if (tdiff) {
        oldtime = now;
        events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, P1_HIT,   EV_BUT_LS_PRESS);
        events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, P2_HIT,   EV_BUT_RS_PRESS);
        events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, P1_POWER, EV_BUT_LP_PRESS);
        events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, P2_POWER, EV_BUT_RP_PRESS);
        events |= do_timer(tdiff, &timer,     EV_TIMER);
        events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
        events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);
        do_timer(tdiff, &lockout_l, 0);
        do_timer(tdiff, &lockout_r, 0);
    }

    if (chk_ev(EV_BUT_LS_PRESS)) Serial.println(F("[BTN] P1 HIT pressed"));
    if (chk_ev(EV_BUT_RS_PRESS)) Serial.println(F("[BTN] P2 HIT pressed"));
    if (chk_ev(EV_BUT_LP_PRESS)) Serial.println(F("[BTN] P1 POWER pressed"));
    if (chk_ev(EV_BUT_RP_PRESS)) Serial.println(F("[BTN] P2 POWER pressed"));

    // Lockout gate for active flight / score states
    if (is_game_state(thestate)) {
        if (lockout_l) events &= ~EV_BUT_LS_PRESS;
        if (lockout_r) events &= ~EV_BUT_RS_PRESS;
    }
    if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
    if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;

    // ── Forfeit detection ────────────────────────────────────
    if (thestate != ST_IDLE &&
        thestate != ST_WIN_L    && thestate != ST_WIN_R &&
        thestate != ST_FORFEIT_L && thestate != ST_FORFEIT_R)
    {
        if (button_is_down(P1_HIT) && button_is_down(P1_POWER)) {
            uint16_t prev = forfeit_hold_l;
            forfeit_hold_l += tdiff;
            if (forfeit_hold_l / 1000 != prev / 1000) {
                Serial.print(F("[FORFEIT] P1 holding: "));
                Serial.print(forfeit_hold_l / 1000);
                Serial.print(F(" / "));
                Serial.print(TIME_FORFEIT / 1000);
                Serial.println(F(" s..."));
            }
            if (forfeit_hold_l >= TIME_FORFEIT) set_state(ST_FORFEIT_L);
        } else {
            if (forfeit_hold_l > 0) {
                Serial.println(F("[FORFEIT] P1 released – hold timer reset"));
                forfeit_hold_l = 0;
            }
        }

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
            if (forfeit_hold_r >= TIME_FORFEIT) set_state(ST_FORFEIT_R);
        } else {
            if (forfeit_hold_r > 0) {
                Serial.println(F("[FORFEIT] P2 released – hold timer reset"));
                forfeit_hold_r = 0;
            }
        }
    }

    // ── State machine ─────────────────────────────────────────
    switch (thestate) {

        // ── IDLE ─────────────────────────────────────────────
        //  ANY button (Hit or Power) starts the game.
        //  Which button started it does not affect serve speed —
        //  the serve-state entry always shows the blink prompt,
        //  and the player chooses Hit or Power when they serve.
        case ST_IDLE:
            if      (chk_ev(EV_BUT_LS_PRESS) || chk_ev(EV_BUT_LP_PRESS)) {
                Serial.println(F("[IDLE] P1 starts game (any button)"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS) || chk_ev(EV_BUT_RP_PRESS)) {
                Serial.println(F("[IDLE] P2 starts game (any button)"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_IDLE;
                animate_idle();
            }
            break;

        // ── START_L: P1 must serve ────────────────────────────
        //  Hit   → normal speed (fixed, predictable)
        //  Power → random speed (unpredictable, harder to return)
        case ST_START_L:
            if      (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);   // normal serve
            } else if (chk_ev(EV_BUT_LP_PRESS)) {
                do_serve_L(1);   // power / random serve
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

        // ── START_R: P2 must serve ────────────────────────────
        case ST_START_R:
            if      (chk_ev(EV_BUT_RS_PRESS)) {
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

        // ── MOVE_LR: ball flying L → R ───────────────────────
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
                if (NPIXELS - 1 - ballpos <= zone_r) {
                    Serial.print(F("[BALL] entering P2 zone  pos=")); Serial.println(ballpos);
                    set_state(ST_ZONE_R);
                }
            }
            break;

        // ── MOVE_RL: ball flying R → L ───────────────────────
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

        // ── ZONE_L: ball in P1's hit zone ────────────────────
        case ST_ZONE_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.print(F("[HIT] P1 returned at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_LR);
                if (zone_l > 1 && button_is_down(P1_POWER)) {
                    zone_l--; boosted = 1; speed_to_timer(); boost_l++;
                    Serial.println(F("[BOOST] P1 power boost!"));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (!ballpos) {
                    Serial.println(F("[MISS] P1 missed! P2 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_r >= WIN_POINTS) set_state(ST_WIN_R);
                    else                          set_state(ST_POINT_R);
                } else {
                    speed_to_timer();
                    ballpos--;
                }
                draw_course(SHOW_LO);
                draw_ball(-1, ballpos);
                one_d.show();
            }
            break;

        // ── ZONE_R: ball in P2's hit zone ────────────────────
        case ST_ZONE_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.print(F("[HIT] P2 returned at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_RL);
                if (zone_r > 1 && button_is_down(P2_POWER)) {
                    zone_r--; boosted = 1; speed_to_timer(); boost_r++;
                    Serial.println(F("[BOOST] P2 power boost!"));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (ballpos == NPIXELS - 1) {
                    Serial.println(F("[MISS] P2 missed! P1 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_l >= WIN_POINTS) set_state(ST_WIN_L);
                    else                          set_state(ST_POINT_L);
                } else {
                    speed_to_timer();
                    ballpos++;
                }
                draw_course(SHOW_LO);
                draw_ball(+1, ballpos);
                one_d.show();
            }
            break;

        // ── POINT_L: P1 just scored ───────────────────────────
        case ST_POINT_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                set_state(ST_RESUME_L);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                for (uint8_t j = 0; j < LEDS_PER_PT; j++) {
                    uint8_t px = NPIXELS / 2 - 1 - ((points_l - 1) * LEDS_PER_PT + j);
                    one_d.setPixelColor(px, (pointblinkcount & 0x01) ? SHOW_HI : 0, 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_L);
            }
            break;

        // ── POINT_R: P2 just scored ───────────────────────────
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

        // ── RESUME_L: P1 scored, P1 re-serves ────────────────
        //  Again: Hit = normal, Power = random
        case ST_RESUME_L:
            if      (chk_ev(EV_BUT_LS_PRESS)) {
                do_serve_L(0);
            } else if (chk_ev(EV_BUT_LP_PRESS)) {
                do_serve_L(1);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── RESUME_R: P2 scored, P2 re-serves ────────────────
        case ST_RESUME_R:
            if      (chk_ev(EV_BUT_RS_PRESS)) {
                do_serve_R(0);
            } else if (chk_ev(EV_BUT_RP_PRESS)) {
                do_serve_R(1);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos,
                    ballblinkstate ? 255 : 0,
                    ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── WIN & FORFEIT ─────────────────────────────────────
        case ST_WIN_L: case ST_WIN_R:
        case ST_FORFEIT_L: case ST_FORFEIT_R:
            if (chk_ev(EV_TONETIMER)) {
                events &= ~EV_TONETIMER;
                tune_next();
            }
            if      (chk_ev(EV_BUT_LS_PRESS)) { Serial.println(F("[RESTART] P1")); set_state(ST_START_L); }
            else if (chk_ev(EV_BUT_RS_PRESS)) { Serial.println(F("[RESTART] P2")); set_state(ST_START_R); }
            else if (chk_ev(EV_TIMER)) {
                timer = TIME_WIN_BLINK;
                uint8_t p2_side = (thestate == ST_WIN_R || thestate == ST_FORFEIT_L);
                if (!animate_win(p2_side)) {
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

    // Global tone-off (not consumed by win/forfeit above)
    if (chk_ev(EV_TONETIMER))
        set_tone(0, 0);
}

// vim: syn=cpp
