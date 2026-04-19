/*
 * ============================================================
 *  1D LED PONG  v1.2  –  Arduino Uno
 * ============================================================
 *  Based on original 1D Pong by B.Stultiens (GPL v3)
 *
 *  CHANGES FROM v1.1:
 *   1. TM1637 4-digit display  ( ---- idle  /  P--O game )
 *   2. Self-contained sounds   ( no notes.h / no separate file )
 *   3. Smooth rainbow fade-in AND fade-out in idle animation
 *   4. RESUME states require player button press  ( no auto-serve )
 *   5. Serial debug output throughout
 *
 *  LIBRARIES  (install via Library Manager):
 *    Adafruit NeoPixel
 *    TM1637Display  (by avishorp)
 *
 *  WIRING:
 *    Pin  2  →  WS2812B DIN (330Ω series R recommended)
 *    Pin  4  →  P2 Hit  button  → GND
 *    Pin  5  →  P2 Power button → GND
 *    Pin  6  →  P1 Hit  button  → GND
 *    Pin  7  →  P1 Power button → GND
 *    Pin  8  →  Passive buzzer +  (– to GND)
 *    Pin 12  →  TM1637 DIO
 *    Pin 13  →  TM1637 CLK
 *    5V / GND → strip, display, buzzer
 * ============================================================
 */

#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>

#define NELEM(x)  (sizeof(x) / sizeof((x)[0]))

// ── Pin assignments ──────────────────────────────────────────
#define PIN_WSDATA   2    // WS2812B data
#define PIN_BUT_RS   4    // P2 hit    (active LOW, INPUT_PULLUP)
#define PIN_BUT_RP   5    // P2 power  (active LOW, INPUT_PULLUP)
#define PIN_BUT_LS   6    // P1 hit    (active LOW, INPUT_PULLUP)
#define PIN_BUT_LP   7    // P1 power  (active LOW, INPUT_PULLUP)
#define PIN_SOUND    8    // Passive buzzer – tone() works on any pin
#define PIN_TM_DIO  12    // TM1637 data
#define PIN_TM_CLK  13    // TM1637 clock

// ── Game parameters ─────────────────────────────────────────
#define NPIXELS          60   // LEDs in strip
#define ZONE_SIZE         7   // Default hit-zone width (LEDs each side)
#define SHOW_LO          12   // Score-dot brightness during flight
#define SHOW_HI          48   // Score-dot brightness at rest / serve
#define WIN_POINTS        9   // First to this wins
#define TONE_INTERVAL     5   // Play move-tick every N ball steps

// ── Event flags (bit-mask) ───────────────────────────────────
#define EV_BUT_LS_PRESS  0x01   // P1 hit pressed
#define EV_BUT_RS_PRESS  0x02   // P2 hit pressed
#define EV_BUT_LP_PRESS  0x04   // P1 power pressed
#define EV_BUT_RP_PRESS  0x08   // P2 power pressed
#define EV_TIMER         0x10   // General timer expired
#define EV_TIMEOUT       0x20   // Idle / start timeout expired
#define EV_TONETIMER     0x40   // Current note duration expired

// ── Timing (milliseconds) ────────────────────────────────────
#define TIME_DEBOUNCE        8       // Button settle time
#define TIME_IDLE           40       // Idle animation tick (≈25 fps)
#define TIME_START_TIMEOUT  20000    // Return to idle if player never serves
#define TIME_BALL_BLINK     150      // Blink period for ball at serve pos
#define TIME_SPEED_MIN       10      // Fastest possible ball step
#define TIME_SPEED_INTERVAL   3      // Speed gain per LED from wall
#define TIME_POINT_BLINK    233      // Score-dot blink after point
#define TIME_WIN_BLINK       85      // Win animation frame period
#define TIME_LOCKOUT        250      // Anti-spam gate (~4 presses/s max)
#define TIME_TONE_BOUNCE     50      // Sound durations (ms)
#define TIME_TONE_MOVE       25
#define TIME_TONE_SCORE      50
#define TIME_TONE_SERVE      50

// ── Game states ─────────────────────────────────────────────
enum {
    ST_IDLE = 0,                  // No game running, rainbow plays
    ST_START_L, ST_START_R,       // Waiting for initial serve
    ST_MOVE_LR, ST_MOVE_RL,       // Ball in flight
    ST_ZONE_L,  ST_ZONE_R,        // Ball in a player's hit zone
    ST_POINT_L, ST_POINT_R,       // A point was just scored
    ST_RESUME_L,ST_RESUME_R,      // Scorer must re-serve (button only)
    ST_WIN_L,   ST_WIN_R,         // Game over
};

// ── Library objects ──────────────────────────────────────────
Adafruit_NeoPixel one_d(NPIXELS, PIN_WSDATA, NEO_GRB | NEO_KHZ800);
TM1637Display     disp(PIN_TM_CLK, PIN_TM_DIO);

// ── TM1637 raw segments ──────────────────────────────────────
#define SEG_DASH   0x40   // Centre bar  ( – )
#define SEG_BLANK  0x00   // All off

// ─────────────────────────────────────────────────────────────
//  SELF-CONTAINED SOUND SYSTEM
//
//  No notes.h needed.  Uses Arduino tone() which works on any
//  digital pin and is non-blocking (returns immediately).
//  tonetimer tracks how long the current note should play;
//  when EV_TONETIMER fires in loop(), tone is silenced (or the
//  win jingle advances to its next note).
// ─────────────────────────────────────────────────────────────
#define SND_BOUNCE  196    // G3  – ball returned / hit
#define SND_TICK    392    // G4  – ball in flight tick
#define SND_SCORE   523    // C5  – point scored
#define SND_SERVE   174    // F3  – serve launched

// Win jingle stored in flash  { frequency Hz, duration ms }
typedef struct { uint16_t freq; uint16_t dur; } note_t;
static const note_t tune_win[] PROGMEM = {
    {1661, 125}, {1760, 125}, {1661, 125}, {1319, 125},   // Gs6 A6 Gs6 E6  ×2
    {1661, 125}, {1760, 125}, {1661, 125}, {1319, 125},
    {   0, 250},                                           // rest
    { 294, 250}, { 294, 250}, { 247, 250}, { 330, 250},   // D4 D4 B3 E4
    { 294, 500}, { 247, 500},                              // D4 B3
    { 294, 250}, { 294, 250}, { 247, 250}, { 330, 250},   // repeat
    { 294, 500}, { 247, 500},
};

// ─────────────────────────────────────────────────────────────
//  GLOBAL GAME STATE
// ─────────────────────────────────────────────────────────────
static uint32_t oldtime;
static uint8_t  thestate;

// Button stable states (1=released, 0=pressed with INPUT_PULLUP)
static uint8_t  bstate_ls, bstate_rs, bstate_lp, bstate_rp;
// Button debounce countdown timers
static uint8_t  debtmr_ls, debtmr_rs, debtmr_lp, debtmr_rp;

static uint16_t timer;        // General-purpose FSM countdown
static uint16_t timeout;      // Start-screen idle timeout
static uint16_t tonetimer;    // Active note remaining duration
static uint16_t lockout_l;    // P1 anti-spam countdown
static uint16_t lockout_r;    // P2 anti-spam countdown

static uint8_t  ballblinkstate;    // Ball blink on/off toggle
static uint8_t  pointblinkcount;   // Blink cycles left after point
static uint8_t  ballpos;           // Current ball LED index
static uint16_t speed;             // ms per ball step
static uint8_t  speedup;           // Cumulative rally speedup
static uint8_t  points_l;          // P1 score
static uint8_t  points_r;          // P2 score
static uint8_t  zone_l, zone_r;    // Current hit-zone size (shrinks on boost)
static uint8_t  boost_l, boost_r;  // How many times each boosted this rally
static uint8_t  boosted;           // Set when ball is currently boosted
static uint8_t  tonecount;         // Move-tick countdown (fires every N steps)
static uint8_t  tuneidx;           // Position in win jingle

// Win animation sub-state
static uint8_t  aw_state;

// Idle animation sub-state
//
//  Idle animation state map:
//   0       rainbow FADE-IN   (brightness  0 → 128)
//   1,2,3   rainbow FULL      (brightness 128, 3 complete hue cycles)
//   4,6     demo ball  L → R
//   5,7     demo ball  R → L
//   8,10    score fill  expand from centre
//   9,11    score fill  collapse to centre
//  12       rainbow FADE-OUT  (brightness 128 → 0)  then → 0
//
static uint16_t ai_h;          // Hue position in the rainbow
static uint8_t  ai_state;      // Which idle sub-state we're in
static uint8_t  ai_pos;        // Ball / fill position scratch
static uint8_t  ai_brightness; // Rainbow brightness (0-128, for fade)

#define H_STEPS  1542   // One full hue revolution in our unit space

// ─────────────────────────────────────────────────────────────
//  SOUND FUNCTIONS
// ─────────────────────────────────────────────────────────────

/*
 * set_tone(freq, duration)
 *  Start a tone at freq Hz for duration ms.
 *  freq=0 → silence.  Returns immediately (non-blocking).
 *  tonetimer drives EV_TONETIMER to know when to stop.
 */
static inline void set_tone(uint16_t freq, uint16_t duration) {
    tonetimer = duration;
    if (freq) tone(PIN_SOUND, freq);
    else      noTone(PIN_SOUND);
}

/*
 * tune_next()
 *  Play the next note of the win jingle.
 *  Called each time EV_TONETIMER fires during ST_WIN_L / ST_WIN_R.
 */
static inline void tune_next() {
    if (tuneidx < NELEM(tune_win)) {
        uint16_t f = pgm_read_word(&tune_win[tuneidx].freq);
        uint16_t d = pgm_read_word(&tune_win[tuneidx].dur);
        set_tone(f, d);
        tuneidx++;
        Serial.print(F("[TUNE] note ")); Serial.print(tuneidx);
        Serial.print(F("/")); Serial.print((uint8_t)NELEM(tune_win));
        Serial.print(F("  freq=")); Serial.println(f);
    } else {
        set_tone(0, 0);
        Serial.println(F("[TUNE] jingle finished"));
    }
}

// ─────────────────────────────────────────────────────────────
//  TM1637 DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────

// Show  ----  (no game running)
static void disp_idle() {
    uint8_t seg[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
    disp.setSegments(seg, 4, 0);
}

// Show  P -- O  (live score, e.g. "3--7")
static void disp_score() {
    uint8_t seg[4] = {
        disp.encodeDigit(points_l),  // left digit  = P1 score
        SEG_DASH,                    // separator
        SEG_DASH,                    // separator
        disp.encodeDigit(points_r)   // right digit = P2 score
    };
    disp.setSegments(seg, 4, 0);
}

// ─────────────────────────────────────────────────────────────
//  BUTTON HELPERS
// ─────────────────────────────────────────────────────────────

/*
 * button_is_down(pin)
 *  Returns non-zero if the button is currently held.
 *  Used to detect simultaneous Hit+Power for speed boost.
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
 *  Filters glitches; fires event only on the falling edge (press).
 *  INPUT_PULLUP → HIGH=released, LOW=pressed.
 */
static inline uint8_t do_debounce(uint8_t tdiff,
                                   uint8_t *bstate, uint8_t *debtmr,
                                   uint8_t pin, uint8_t ev)
{
    if (!*debtmr) {
        uint8_t state = digitalRead(pin);
        if (state != *bstate) {
            *debtmr = TIME_DEBOUNCE;           // start settle timer
            if (!(*bstate = state)) return ev; // HIGH→LOW = press event
        }
    } else {
        if (*debtmr >= tdiff) *debtmr -= tdiff;
        else                  *debtmr = 0;
    }
    return 0;
}

/*
 * do_timer()
 *  Counts down *tmr; fires ev exactly once when it reaches zero.
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
 *  Lights the hit-zones at both ends of the strip (teal).
 *  Zone size can shrink when a player uses the Power button.
 */
static void draw_sides() {
    // Left zone (P1)
    for (uint8_t i = 0; i < zone_l - 1; i++)
        one_d.setPixelColor(i, 0, 64, 64);
    one_d.setPixelColor(0, 0, 64, 64);               // always mark pixel 0
    // Right zone (P2)
    for (uint8_t i = 0; i < zone_r - 1; i++)
        one_d.setPixelColor(NPIXELS - 1 - i, 0, 64, 64);
    one_d.setPixelColor(NPIXELS - 1, 0, 64, 64);     // always mark last pixel
}

/*
 * draw_ball(dir, pos)
 *  Yellow ball at pos with 5-pixel diminishing tail (dir = travel direction).
 *  dir=+1 (L→R) tail goes leftward; dir=-1 (R→L) tail goes rightward.
 */
static void draw_ball(int8_t dir, uint8_t pos) {
    uint8_t c = 255;
    for (uint8_t i = 0; i < 5 && pos < NPIXELS; i++) {
        one_d.setPixelColor(pos, c, c, 0);  // yellow
        c >>= 1;                             // half brightness each step
        pos -= dir;                          // tail walks opposite to travel
        // Note: pos is uint8_t; going below 0 wraps to 255 → exits < NPIXELS check
    }
}

/*
 * draw_course(v)
 *  Clear strip, redraw zones, and (if v>0) draw score dots.
 *  P1 score = red  dots expanding leftward from centre.
 *  P2 score = green dots expanding rightward from centre.
 *  v=SHOW_LO during flight; v=SHOW_HI at rest.
 */
static void draw_course(uint8_t v) {
    one_d.clear();
    draw_sides();
    if (v) {
        for (uint8_t i = 0; i < points_l; i++) {
            one_d.setPixelColor(NPIXELS/2 - 1 - (2*i+0), v, 0, 0);
            one_d.setPixelColor(NPIXELS/2 - 1 - (2*i+1), v, 0, 0);
        }
        for (uint8_t i = 0; i < points_r; i++) {
            one_d.setPixelColor(NPIXELS/2 + (2*i+0), 0, v, 0);
            one_d.setPixelColor(NPIXELS/2 + (2*i+1), 0, v, 0);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  IDLE ANIMATION
// ─────────────────────────────────────────────────────────────

static void animate_idle_init() {
    ai_h          = 0;
    ai_state      = 0;
    ai_pos        = 0;
    ai_brightness = 0;   // start at black; fade-in begins immediately
    Serial.println(F("[IDLE] animation reset"));
}

/*
 * draw_rainbow(brightness)
 *  Renders the full-strip rainbow at the given brightness (0-255).
 *  Does NOT advance ai_h; caller handles that.
 */
static void draw_rainbow(uint8_t brightness) {
    for (uint8_t i = 0; i < NPIXELS; i++) {
        uint16_t h = ai_h + (i << 4);
        if (h >= H_STEPS) h -= H_STEPS;
        // Scale hue: H_STEPS(1542) → ColorHSV range(0-65535); 65535/1542 ≈ 42
        one_d.setPixelColor(i, one_d.ColorHSV((uint32_t)h * 42, 255, brightness));
    }
}

/*
 * animate_idle()
 *  Called every TIME_IDLE ms (40 ms ≈ 25 fps) while in ST_IDLE.
 *
 *  Full sequence (looping):
 *   State  0       Fade-in  rainbow 0→128 brightness (~1.3 s)
 *   State  1,2,3   Full     rainbow at 128 brightness (~7.5 s, 3 hue cycles)
 *   State  4,6     Demo ball rolling L→R
 *   State  5,7     Demo ball rolling R→L
 *   State  8,10    Score fill expanding outward
 *   State  9,11    Score fill collapsing inward
 *   State  12      Fade-out rainbow 128→0 brightness (~1.3 s) → back to 0
 */
static void animate_idle() {
    switch (ai_state) {

        // ── State 0: rainbow FADE-IN ──────────────────────────
        case 0:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness < 124) {
                ai_brightness += 4;       // step up each frame
            } else {
                ai_brightness = 128;      // clamp to target
                ai_state = 1;             // done fading in
                Serial.println(F("[IDLE] fade-in done → full rainbow"));
            }
            break;

        // ── States 1-3: full-brightness rainbow (3 hue cycles) ─
        case 1: case 2: case 3:
            draw_rainbow(128);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) {
                ai_h -= H_STEPS;          // wrapped = one full cycle complete
                ai_pos = 0;
                ai_state++;               // after 3 wraps: state 4 = ball
            }
            break;

        // ── States 4,6: demo ball rolling L → R ──────────────
        case 4: case 6:
            draw_course(0);
            draw_ball(1, ai_pos++);
            if (ai_pos >= NPIXELS) ai_state++;
            break;

        // ── States 5,7: demo ball rolling R → L ──────────────
        case 5: case 7:
            draw_course(0);
            draw_ball(-1, --ai_pos);
            if (!ai_pos) ai_state++;
            break;

        // ── States 8,10: score fill expanding ─────────────────
        case 8: case 10:
            draw_course(0);
            for (uint8_t i = 0; i < ai_pos; i++) {
                one_d.setPixelColor(NPIXELS/2 - 1 - i, 255, 0, 0);  // red left
                one_d.setPixelColor(NPIXELS/2 + i,     0, 255, 0);  // green right
            }
            if (++ai_pos >= NPIXELS/2) { ai_state++; ai_pos = 0; }
            break;

        // ── States 9,11: score fill collapsing ────────────────
        case 9: case 11:
            draw_course(0);
            for (uint8_t i = 0; i < NPIXELS/2 - ai_pos; i++) {
                one_d.setPixelColor(NPIXELS/2 - 1 - i, 255, 0, 0);
                one_d.setPixelColor(NPIXELS/2 + i,     0, 255, 0);
            }
            if (++ai_pos >= NPIXELS/2) { ai_state++; ai_pos = 0; }
            break;

        // ── State 12: rainbow FADE-OUT ────────────────────────
        // ai_brightness is still 128 from the rainbow phase;
        // fade it back to 0 then restart the whole sequence.
        case 12:
            draw_rainbow(ai_brightness);
            ai_h += H_STEPS / 60;
            if (ai_h >= H_STEPS) ai_h -= H_STEPS;
            if (ai_brightness > 4) {
                ai_brightness -= 4;       // step down each frame
            } else {
                ai_brightness = 0;
                ai_state      = 0;        // loop: back to fade-in
                Serial.println(F("[IDLE] fade-out done → looping"));
            }
            break;

        default:
            ai_state      = 0;
            ai_brightness = 0;
            break;
    }
    one_d.show();
}

// ─────────────────────────────────────────────────────────────
//  WIN ANIMATION  (unchanged from original)
// ─────────────────────────────────────────────────────────────

static void animate_win_init() {
    aw_state = 0;
}

/*
 * animate_win(side)
 *  Runs a multi-phase wipe animation on the winner's half.
 *  side=0 → P1 (left, red);  side=1 → P2 (right, green).
 *  Returns 1 while running, 0 when complete → triggers ST_IDLE.
 */
static uint8_t animate_win(uint8_t side) {
    uint32_t clr;
    uint8_t  pos;
    if (side) {
        clr = Adafruit_NeoPixel::Color(0, 255, 0);   // P2 green
        pos = NPIXELS / 2;
    } else {
        clr = Adafruit_NeoPixel::Color(255, 0, 0);   // P1 red
        pos = 0;
    }
    one_d.clear();
    if      (aw_state < 20)  { if (aw_state & 0x01) for (uint8_t i=0;i<NPIXELS/2;i++) one_d.setPixelColor(pos+i, clr); }
    else if (aw_state < 50)  { for (uint8_t i=0; i<aw_state-20;      i++) one_d.setPixelColor(pos+i,          clr); }
    else if (aw_state < 80)  { for (uint8_t i=aw_state-50; i<NPIXELS/2; i++) one_d.setPixelColor(pos+i,       clr); }
    else if (aw_state < 110) { for (uint8_t i=0; i<aw_state-80;      i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    else if (aw_state < 140) { for (uint8_t i=aw_state-110; i<NPIXELS/2; i++) one_d.setPixelColor(NPIXELS/2-1-i+pos, clr); }
    one_d.show();
    return ++aw_state < 140;
}

// ─────────────────────────────────────────────────────────────
//  STATE MACHINE HELPERS
// ─────────────────────────────────────────────────────────────

/*
 * is_game_state(s)
 *  Returns 1 for states where the lockout timer should gate button presses.
 *  Prevents mashing from doing anything unexpected during flight/score.
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
 *  Sets `timer` to the current ball step interval.
 *  Boosted ball travels 25% faster.  Minimum clamped to 2 ms.
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
 *  Runs EXIT actions for old state, then ENTRY actions for new state.
 *  All LED / display / sound setup triggered by a state change lives here.
 */
static void set_state(uint8_t newstate) {

    Serial.print(F("[STATE] "));
    Serial.print(thestate);
    Serial.print(F(" -> "));
    Serial.println(newstate);

    // ── EXIT ACTIONS ─────────────────────────────────────────
    switch (thestate) {

        case ST_IDLE:
        case ST_WIN_L:
        case ST_WIN_R:
            // Full reset when a new game begins
            points_l = points_r = 0;
            boost_l  = boost_r  = 0;
            zone_l   = zone_r   = ZONE_SIZE;
            speedup  = 0;
            boosted  = 0;
            Serial.println(F("[RESET] scores and zones reset for new game"));
            break;

        // P1 side about to serve → ball spawns at position 0
        case ST_START_L:
        case ST_POINT_L:
        case ST_RESUME_L:
            ballpos = 0;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;  // moderate serve speed
            speedup = 0;
            break;

        // P2 side about to serve → ball spawns at last position
        case ST_START_R:
        case ST_POINT_R:
        case ST_RESUME_R:
            ballpos = NPIXELS - 1;
            speed   = TIME_SPEED_MIN + 5 * TIME_SPEED_INTERVAL;
            speedup = 0;
            break;

        case ST_ZONE_L:
            // Ball's distance from P1's wall when hit determines return speed
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
            if (++speedup / 2 >= speed) speed = 2;   // clamp floor
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] new ball speed=")); Serial.println(speed);
            break;

        case ST_ZONE_R:
            speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS - 1 - ballpos);
            if (++speedup / 2 >= speed) speed = 2;
            else                        speed -= speedup / 2;
            boosted = 0;
            Serial.print(F("[SPEED] new ball speed=")); Serial.println(speed);
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
            disp_idle();                              // show ----
            Serial.println(F("[IDLE] waiting for player – rainbow playing – display: ----"));
            break;

        // ── START screens (awaiting initial serve) ────────────
        case ST_START_L:
        case ST_START_R:
            draw_course(SHOW_HI);
            one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = TIME_START_TIMEOUT;      // 20 s idle timeout for PRE-GAME only
            ballblinkstate = 0;
            ballpos        = (thestate == ST_START_L) ? 0 : NPIXELS - 1;
            disp_score();                             // show 0--0
            Serial.print(F("[START] "));
            Serial.print(thestate == ST_START_L ? "P1" : "P2");
            Serial.println(F(" should hit to serve  (20 s timeout → idle)"));
            break;

        // ── Ball in flight ────────────────────────────────────
        case ST_MOVE_LR:
        case ST_MOVE_RL:
            speed_to_timer();
            tonecount = TONE_INTERVAL;
            disp_score();
            Serial.print(F("[BALL] launched  speed="));
            Serial.print(speed);
            Serial.print(F("ms  dir="));
            Serial.println(thestate == ST_MOVE_LR ? "L→R" : "R→L");
            break;

        // ── Point scored ──────────────────────────────────────
        case ST_POINT_L:
        case ST_POINT_R:
            pointblinkcount = 7;
            // Recover a zone LED if player didn't boost last rally
            if (!boost_l && zone_l < ZONE_SIZE) zone_l++;
            if (!boost_r && zone_r < ZONE_SIZE) zone_r++;
            timer     = TIME_POINT_BLINK;
            if (boost_l) boost_l--;
            if (boost_r) boost_r--;
            lockout_l = lockout_r = TIME_LOCKOUT;     // let the blink play out
            disp_score();
            Serial.print(F("[SCORE] P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));      Serial.print(points_r);
            Serial.print(F("  (need "));   Serial.print(WIN_POINTS);
            Serial.println(F(" to win)"));
            break;

        // ── RESUME – scorer must re-serve (NO auto-timeout) ───
        // FIX: timeout is set to 0.  Only a button press starts the ball.
        // This prevents the game auto-launching while a player is not ready.
        case ST_RESUME_L:
        case ST_RESUME_R:
            draw_course(SHOW_HI);
            one_d.show();
            timer          = TIME_BALL_BLINK;
            timeout        = 0;                       // disabled – button-only
            ballblinkstate = 0;
            disp_score();
            Serial.print(F("[WAIT] "));
            Serial.print(thestate == ST_RESUME_L ? "P1" : "P2");
            Serial.println(F(" press Hit to serve  (no auto-fire)"));
            break;

        // ── Win ───────────────────────────────────────────────
        case ST_WIN_L:
        case ST_WIN_R:
            lockout_l = lockout_r = 2 * TIME_LOCKOUT;
            animate_win_init();
            timer   = TIME_WIN_BLINK;
            tuneidx = 0;
            tune_next();                              // start jingle
            disp_score();                             // keep final score on display
            Serial.print(F("[WIN] "));
            Serial.println(thestate == ST_WIN_L ? "P1 (LEFT)" : "P2 (RIGHT)");
            Serial.print(F("[WIN] Final score  P1=")); Serial.print(points_l);
            Serial.print(F("  P2="));                  Serial.println(points_r);
            Serial.println(F("[WIN] Press any Hit button to restart"));
            break;
    }
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    Serial.println(F("============================"));
    Serial.println(F("  1D PONG  v1.2  – STARTUP"));
    Serial.println(F("============================"));
    Serial.print(F("  NPIXELS   = ")); Serial.println(NPIXELS);
    Serial.print(F("  WIN_POINTS= ")); Serial.println(WIN_POINTS);
    Serial.print(F("  ZONE_SIZE = ")); Serial.println(ZONE_SIZE);

    // Ensure no floating inputs on unused port pins
    PORTB = PORTC = PORTD = 0xff;

    // Button inputs – pulled HIGH; pressing connects to GND (active LOW)
    pinMode(PIN_BUT_LS, INPUT_PULLUP);
    pinMode(PIN_BUT_RS, INPUT_PULLUP);
    pinMode(PIN_BUT_LP, INPUT_PULLUP);
    pinMode(PIN_BUT_RP, INPUT_PULLUP);
    Serial.println(F("  Buttons  → INPUT_PULLUP"));

    // Buzzer output
    digitalWrite(PIN_SOUND, LOW);
    pinMode(PIN_SOUND, OUTPUT);
    Serial.println(F("  Buzzer   → Pin 8 (tone())"));

    // TM1637 display
    disp.setBrightness(5);     // 0-7; 5 is comfortable indoors
    disp_idle();               // show ---- immediately
    Serial.println(F("  TM1637  → Pins 12/13  brightness=5"));

    // WS2812B LED strip
    one_d.begin();
    one_d.show();              // all off
    Serial.println(F("  LEDs    → Pin 2"));

    // Initialise FSM (runs exit + entry for ST_IDLE)
    thestate = ST_IDLE;
    set_state(ST_IDLE);

    Serial.println(F("============================"));
    Serial.println(F("  Ready – press P1 or P2 Hit"));
    Serial.println(F("============================"));
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────

#define chk_ev(ev)  (events & (ev))

void loop() {
    uint32_t now;
    uint8_t tdiff  = (now = millis()) - oldtime;
    uint8_t events = 0;

    // ── Sample buttons and tick timers every ms ──────────────
    if (tdiff) {
        oldtime = now;

        events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, PIN_BUT_LS, EV_BUT_LS_PRESS);
        events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, PIN_BUT_RS, EV_BUT_RS_PRESS);
        events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, PIN_BUT_LP, EV_BUT_LP_PRESS);
        events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, PIN_BUT_RP, EV_BUT_RP_PRESS);

        events |= do_timer(tdiff, &timer,     EV_TIMER);
        events |= do_timer(tdiff, &timeout,   EV_TIMEOUT);
        events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);
        do_timer(tdiff, &lockout_l, 0);
        do_timer(tdiff, &lockout_r, 0);
    }

    // ── Serial report on any button press ───────────────────
    if (chk_ev(EV_BUT_LS_PRESS)) Serial.println(F("[BTN] P1 HIT pressed"));
    if (chk_ev(EV_BUT_RS_PRESS)) Serial.println(F("[BTN] P2 HIT pressed"));
    if (chk_ev(EV_BUT_LP_PRESS)) Serial.println(F("[BTN] P1 POWER pressed"));
    if (chk_ev(EV_BUT_RP_PRESS)) Serial.println(F("[BTN] P2 POWER pressed"));

    // ── Lockout gate: suppress button events during active play
    if (is_game_state(thestate)) {
        if (lockout_l) events &= ~EV_BUT_LS_PRESS;
        if (lockout_r) events &= ~EV_BUT_RS_PRESS;
    }

    // Any accepted hit press re-arms that player's lockout window
    if (chk_ev(EV_BUT_LS_PRESS)) lockout_l = TIME_LOCKOUT;
    if (chk_ev(EV_BUT_RS_PRESS)) lockout_r = TIME_LOCKOUT;

    // ── State machine ─────────────────────────────────────────
    switch (thestate) {

        // ── IDLE: rainbow plays; display shows ---- ──────────
        case ST_IDLE:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[IDLE] P1 starts game"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[IDLE] P2 starts game"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_IDLE;
                animate_idle();
            }
            break;

        // ── START_L: waiting for P1 to hit the serve ─────────
        case ST_START_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[SERVE] P1 served"));
                set_state(ST_MOVE_LR);
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20 s timeout → idle"));
                set_state(ST_IDLE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos, ballblinkstate ? 255 : 0,
                                             ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── START_R: waiting for P2 to hit the serve ─────────
        case ST_START_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[SERVE] P2 served"));
                set_state(ST_MOVE_RL);
            } else if (chk_ev(EV_TIMEOUT)) {
                Serial.println(F("[START] 20 s timeout → idle"));
                set_state(ST_IDLE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos, ballblinkstate ? 255 : 0,
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
                draw_ball(1, ballpos);
                one_d.show();
                ballpos++;
                if (NPIXELS - 1 - ballpos <= zone_r) {
                    Serial.print(F("[BALL] entering P2 zone  pos=")); Serial.println(ballpos);
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
                if (ballpos <= zone_l) {
                    Serial.print(F("[BALL] entering P1 zone  pos=")); Serial.println(ballpos);
                    set_state(ST_ZONE_L);
                }
            }
            break;

        // ── ZONE_L: ball in P1's hit zone ─────────────────────
        case ST_ZONE_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.print(F("[HIT] P1 returned ball at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_LR);
                // Power boost: simultaneously holding Power button shrinks zone
                if (zone_l > 1 && button_is_down(PIN_BUT_LP)) {
                    zone_l--;
                    boosted = 1;
                    speed_to_timer();
                    boost_l++;
                    Serial.println(F("[BOOST] P1 power boost!"));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (!ballpos) {
                    // Ball hit P1's wall → P2 scores
                    Serial.println(F("[MISS] P1 missed! P2 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_r >= WIN_POINTS) {
                        Serial.println(F("[WIN] P2 wins!"));
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

        // ── ZONE_R: ball in P2's hit zone ─────────────────────
        case ST_ZONE_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.print(F("[HIT] P2 returned ball at pos=")); Serial.println(ballpos);
                set_tone(SND_BOUNCE, TIME_TONE_BOUNCE);
                set_state(ST_MOVE_RL);
                if (zone_r > 1 && button_is_down(PIN_BUT_RP)) {
                    zone_r--;
                    speed_to_timer();
                    boosted = 1;
                    boost_r++;
                    Serial.println(F("[BOOST] P2 power boost!"));
                }
            } else if (chk_ev(EV_TIMER)) {
                if (ballpos == NPIXELS - 1) {
                    // Ball hit P2's wall → P1 scores
                    Serial.println(F("[MISS] P2 missed! P1 scores"));
                    set_tone(SND_SCORE, TIME_TONE_SCORE);
                    if (++points_l >= WIN_POINTS) {
                        Serial.println(F("[WIN] P1 wins!"));
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

        // ── POINT_L: P1 scored – blink the new red score dot ──
        case ST_POINT_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                set_state(ST_RESUME_L);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                uint8_t d0 = NPIXELS/2 - 1 - (2*(points_l-1)+0);
                uint8_t d1 = NPIXELS/2 - 1 - (2*(points_l-1)+1);
                if (pointblinkcount & 0x01) {
                    one_d.setPixelColor(d0, 255, 0, 0);
                    one_d.setPixelColor(d1, 255, 0, 0);
                } else {
                    one_d.setPixelColor(d0, 0, 0, 0);
                    one_d.setPixelColor(d1, 0, 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_L);
            }
            break;

        // ── POINT_R: P2 scored – blink the new green score dot ─
        case ST_POINT_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                set_state(ST_RESUME_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_POINT_BLINK;
                draw_course(SHOW_HI);
                uint8_t d0 = NPIXELS/2 + (2*(points_r-1)+0);
                uint8_t d1 = NPIXELS/2 + (2*(points_r-1)+1);
                if (pointblinkcount & 0x01) {
                    one_d.setPixelColor(d0, 0, 255, 0);
                    one_d.setPixelColor(d1, 0, 255, 0);
                } else {
                    one_d.setPixelColor(d0, 0, 0, 0);
                    one_d.setPixelColor(d1, 0, 0, 0);
                }
                one_d.show();
                if (!--pointblinkcount) set_state(ST_RESUME_R);
            }
            break;

        // ── RESUME_L: P1 scored; ONLY P1's button starts next ball ──
        // FIX: EV_TIMEOUT removed. timeout=0 in entry. No auto-fire.
        case ST_RESUME_L:
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[SERVE] P1 re-serves"));
                set_state(ST_MOVE_LR);
                set_tone(SND_SERVE, TIME_TONE_SERVE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos, ballblinkstate ? 255 : 0,
                                             ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── RESUME_R: P2 scored; ONLY P2's button starts next ball ──
        case ST_RESUME_R:
            if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[SERVE] P2 re-serves"));
                set_state(ST_MOVE_RL);
                set_tone(SND_SERVE, TIME_TONE_SERVE);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_BALL_BLINK;
                one_d.setPixelColor(ballpos, ballblinkstate ? 255 : 0,
                                             ballblinkstate ? 128 : 0, 0);
                one_d.show();
                ballblinkstate = !ballblinkstate;
            }
            break;

        // ── WIN_L / WIN_R: jingle + wipe animation ────────────
        case ST_WIN_L:
        case ST_WIN_R:
            if (chk_ev(EV_TONETIMER)) {
                events &= ~EV_TONETIMER;   // consume so the global handler below doesn't silence it
                tune_next();
            }
            if (chk_ev(EV_BUT_LS_PRESS)) {
                Serial.println(F("[RESTART] P1 starts new game"));
                set_state(ST_START_L);
            } else if (chk_ev(EV_BUT_RS_PRESS)) {
                Serial.println(F("[RESTART] P2 starts new game"));
                set_state(ST_START_R);
            } else if (chk_ev(EV_TIMER)) {
                timer = TIME_WIN_BLINK;
                if (!animate_win(thestate == ST_WIN_R))
                    set_state(ST_IDLE);    // animation complete → idle
            }
            break;

        default:
            Serial.println(F("[ERROR] unknown state – resetting"));
            set_state(ST_IDLE);
            break;
    }

    // ── Global tone-off: silence buzzer when note duration ends ──
    // Handled here so every state benefits without individual code.
    // (WIN states consume EV_TONETIMER above to advance jingle instead.)
    if (chk_ev(EV_TONETIMER))
        set_tone(0, 0);
}

// vim: syn=cpp
