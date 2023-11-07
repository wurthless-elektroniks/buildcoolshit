/**
 * Lightweight WS2812B LED driver for use with letter boards
 * 
 * This targets the ubiquitous letter boards you can get on Amazon or at your local dollar store
 * and the LED tape you can get easily off eBay/Aliexpress/etc.
 * 
 * A variant on avr-ws2812 (https://github.com/stephendpmurphy/avr-ws2812) is used as the LED driver,
 * and has been modified to work on 4bpp data (saves a huge amount of work RAM).
 */

#include "avr_ws2812.h"

// #define BIG_BOY for 12"x12" signs
// else we assume 6"x6"
#define BIG_BOY

// #define HIGH_DENSITY for higher-count LED strips
#define HIGH_DENSITY

#define LOW_DENSITY_LED_COUNT (6+6+6+6)
#define HI_DENSITY_LED_COUNT (12+11+11+10)
#define HI_DENSITY_BIG_BOY_LED_COUNT (18+17+17+16)

#ifdef HIGH_DENSITY
  #ifdef BIG_BOY
    #define NUM_LEDS HI_DENSITY_BIG_BOY_LED_COUNT
  #else
    #define NUM_LEDS HI_DENSITY_LED_COUNT
  #endif
#else
  #define NUM_LEDS LOW_DENSITY_LED_COUNT
#endif

#define MS_PER_TICK 10

#define M_MsToTick(ms) (ms / MS_PER_TICK)

#define AUTOADVANCE_TICK_COUNT M_MsToTick(30 * 1000)

// if you want to use a specific mode and only that mode, set these defines appropriately.
// #define USE_THIS_MODE_ONLY RAINBOW
// #define INCLUDE_RAINBOW

// default is to include all modes
#ifndef USE_THIS_MODE_ONLY
  #define INCLUDE_CHASER
  #define INCLUDE_RAINBOW
  #define INCLUDE_FLOOD
  #define INCLUDE_CHAOS
  #define INCLUDE_HALF_FRAME_FLOOD
  #define INCLUDE_PACMAN
  #define INCLUDE_QUARTER_FRAME_FLOOD
  #define INCLUDE_SUPERCOMPUTER
  #define INCLUDE_ROBOTRON
#endif

////////////////////////////////////////////////////////////////////////
// typedefs
////////////////////////////////////////////////////////////////////////
typedef enum {
 STUB = -1,
#ifdef INCLUDE_CHASER
 CHASER,
#endif
#ifdef INCLUDE_RAINBOW
 RAINBOW,
#endif
#ifdef INCLUDE_FLOOD
 FLOOD,
#endif
#ifdef INCLUDE_CHAOS
 CHAOS,
#endif
#ifdef INCLUDE_HALF_FRAME_FLOOD
 HALF_FRAME_FLOOD,
#endif
#ifdef INCLUDE_PACMAN
 PACMAN,
#endif
#ifdef INCLUDE_QUARTER_FRAME_FLOOD
 QUARTER_FRAME_FLOOD,
#endif
#ifdef INCLUDE_SUPERCOMPUTER
 SUPERCOMPUTER,
#endif
#ifdef INCLUDE_ROBOTRON
 ROBOTRON,
#endif
 NUMBER_OF_MODES
} bcs_state_t;

// all functions return number of ticks to wait until next tick (0 = execute next immediately)
typedef uint16_t(*bcs_tick_fcn_t)();

////////////////////////////////////////////////////////////////////////
// global vars
////////////////////////////////////////////////////////////////////////

// current state only kept track of if we switch between modes
#ifndef USE_THIS_MODE_ONLY
uint8_t currentState;
#endif


uint16_t leds[NUM_LEDS]; // these are packed 4bpp ----gggg rrrrbbbb, there's simply not enough memory to store 8bpp

// VERY aggressive sizecoding - saves bytes in reset().
// anything in here will get clobbered when reset() is called.
// beware that modifying these is dangerous.
uint8_t globalStateVars[ sizeof(unsigned long) + sizeof(unsigned long) + sizeof(uint16_t)];
#define loopTickNextOut       (*((unsigned long*)((uint8_t*)&globalStateVars[0])))
#define ticksElapsedThisState (*((unsigned long*)((uint8_t*)&globalStateVars[sizeof(unsigned long)])))
#define waitCounter           (*((uint16_t*)((uint8_t*)&globalStateVars[sizeof(unsigned long) + sizeof(unsigned long)])))

// stores all variables specific to that state.
// everything is zeroed when reset() is called.
uint8_t localStateVars[6];
#define localStateTick (*((uint16_t*)(&localStateVars[4])))

////////////////////////////////////////////////////////////////////////

void ledsReset() {
  blankAllLeds();
}

void ledsSend() {
  ws2812_setleds_4bpp( leds, NUM_LEDS );
}

uint8_t inline waitForWaitCounterExpiry() {
  if (waitCounter != 0) {
    waitCounter --;
    return 0;
  }
  return 1;
}

void blankAllLeds() {
  for (int i = 0; i < NUM_LEDS; i ++) {
    leds[i] = 0;
  }
}

////////////////////////////////////////////////////////////////////////
// chaser effect
////////////////////////////////////////////////////////////////////////

#define M_SetWhite(_leds,i) _leds[i] = M_Pack_4bpp(0xF,0xF,0xF);

#ifdef HIGH_DENSITY
  #define CHASER_STEP 4
#else
  #define CHASER_STEP 3
#endif

uint16_t chaserTick() {
  blankAllLeds();
  for (int i = localStateVars[0]; i < NUM_LEDS; i += CHASER_STEP) M_SetWhite(leds,i);
  localStateVars[0] ++;
  if (localStateVars[0] > (CHASER_STEP-1)) localStateVars[0] = 0;
  return M_MsToTick(150);
}

//////////////////////////////////////////////////
// flood-fill schenanigans
//////////////////////////////////////////////////

#define floodCurrentLed    localStateVars[0]
#define floodCurrentColor  localStateVars[1]

PROGMEM const uint16_t g_flood_colors[] = {
  M_Pack_4bpp(255>>4,0,0),          // red
  M_Pack_4bpp(255>>4,96>>4,0),      // orange
  M_Pack_4bpp(255>>4,255>>4,0),     // yellow
  M_Pack_4bpp(0,255>>4,0),          // green
  M_Pack_4bpp(0,0,255>>4),          // blue
  M_Pack_4bpp(255>>4,0,255>>4),     // purple
  M_Pack_4bpp(0xF,0xF,0xF),         // white
  M_Pack_4bpp(0,0,0),               // off (do NOT change this, behavior in floodTickCommon() depends on it)
};

#define M_Get_Floodtable_Color(x) pgm_read_word_near(&g_flood_colors[x])

#ifdef HIGH_DENSITY
  #ifdef BIG_BOY
    #define FLOOD_RATE M_MsToTick(10)
  #else
    #define FLOOD_RATE M_MsToTick(20)
  #endif
#else
  #define FLOOD_RATE M_MsToTick(50)
#endif

void floodTickFill(uint8_t divider, uint8_t l, uint8_t r, uint8_t step, uint16_t color) {
  if (divider > 0) {
    floodTickFill(divider - 1, l, r >> 1, step,color);
    floodTickFill(divider - 1, r >> 1, r, step,color);
  } else {
    if ( (l + step) < NUM_LEDS) leds[l + step] = color;
  }
}

/**
 * divider: 0 = fill whole frame, 1 = fill halves, 2 = fill quarters.
 */
uint16_t floodTickCommon(uint8_t divider) {
  // if last and first LED blank, reset state machine
  if (leds[0] == 0 && leds[NUM_LEDS-1] == 0) {
    floodCurrentLed = 0;
    floodCurrentColor = 0;
  }

  floodTickFill(divider,0,NUM_LEDS,floodCurrentLed,M_Get_Floodtable_Color(floodCurrentColor));
  floodCurrentLed ++;

  if (floodCurrentLed >= (NUM_LEDS >> divider)) {
    floodCurrentLed = 0;
    floodCurrentColor ++;
    floodCurrentColor &= 7;
    return M_MsToTick(250);
  } else {
    return (FLOOD_RATE << divider);
  }
}

// we assume compiler optimization helps us here, if it doesn't, too bad...

#ifdef INCLUDE_FLOOD
uint16_t floodTick() {
  return floodTickCommon(0);
}
#endif

#ifdef INCLUDE_HALF_FRAME_FLOOD
uint16_t halfFrameFloodTick() {
  return floodTickCommon(1);
}
#endif

#ifdef INCLUDE_QUARTER_FRAME_FLOOD
uint16_t quarterFrameFloodTick() {
  return floodTickCommon(2);
}
#endif

//////////////////////////////////////////////////////////////////////
// rainbow functions
//
// this is a cut down version of how FastLED does it.
// buildcoolshit formerly used FastLED, but FastLED is not compatible
// with ATTiny402s, so that dependency had to be removed altogether.
//////////////////////////////////////////////////////////////////////
#ifdef INCLUDE_RAINBOW

#define APPLY_DIMMING(X) (X)
#define HSV_SECTION_3 (0x40)

struct CHSV {
  uint8_t hue;
  uint8_t sat;
  uint8_t val;
};

struct CRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// ASM version gave compile errors...
void hsv2rgb_raw_C (const struct CHSV & hsv, struct CRGB & rgb)
{
    // Convert hue, saturation and brightness ( HSV/HSB ) to RGB
    // "Dimming" is used on saturation and brightness to make
    // the output more visually linear.

    // Apply dimming curves
    uint8_t value = APPLY_DIMMING( hsv.val);
    uint8_t saturation = hsv.sat;

    // The brightness floor is minimum number that all of
    // R, G, and B will be set to.
    uint8_t invsat = APPLY_DIMMING( 255 - saturation);
    uint8_t brightness_floor = (value * invsat) / 256;

    // The color amplitude is the maximum amount of R, G, and B
    // that will be added on top of the brightness_floor to
    // create the specific hue desired.
    uint8_t color_amplitude = value - brightness_floor;

    // Figure out which section of the hue wheel we're in,
    // and how far offset we are withing that section
    uint8_t section = hsv.hue / HSV_SECTION_3; // 0..2
    uint8_t offset = hsv.hue % HSV_SECTION_3;  // 0..63

    uint8_t rampup = offset; // 0..63
    uint8_t rampdown = (HSV_SECTION_3 - 1) - offset; // 63..0

    // We now scale rampup and rampdown to a 0-255 range -- at least
    // in theory, but here's where architecture-specific decsions
    // come in to play:
    // To scale them up to 0-255, we'd want to multiply by 4.
    // But in the very next step, we multiply the ramps by other
    // values and then divide the resulting product by 256.
    // So which is faster?
    //   ((ramp * 4) * othervalue) / 256
    // or
    //   ((ramp    ) * othervalue) /  64
    // It depends on your processor architecture.
    // On 8-bit AVR, the "/ 256" is just a one-cycle register move,
    // but the "/ 64" might be a multicycle shift process. So on AVR
    // it's faster do multiply the ramp values by four, and then
    // divide by 256.
    // On ARM, the "/ 256" and "/ 64" are one cycle each, so it's
    // faster to NOT multiply the ramp values by four, and just to
    // divide the resulting product by 64 (instead of 256).
    // Moral of the story: trust your profiler, not your insticts.

    // Since there's an AVR assembly version elsewhere, we'll
    // assume what we're on an architecture where any number of
    // bit shifts has roughly the same cost, and we'll remove the
    // redundant math at the source level:

    //  // scale up to 255 range
    //  //rampup *= 4; // 0..252
    //  //rampdown *= 4; // 0..252

    // compute color-amplitude-scaled-down versions of rampup and rampdown
    uint8_t rampup_amp_adj   = (rampup   * color_amplitude) / (256 / 4);
    uint8_t rampdown_amp_adj = (rampdown * color_amplitude) / (256 / 4);

    // add brightness_floor offset to everything
    uint8_t rampup_adj_with_floor   = rampup_amp_adj   + brightness_floor;
    uint8_t rampdown_adj_with_floor = rampdown_amp_adj + brightness_floor;


    if( section ) {
        if( section == 1) {
            // section 1: 0x40..0x7F
            rgb.r = brightness_floor;
            rgb.g = rampdown_adj_with_floor;
            rgb.b = rampup_adj_with_floor;
        } else {
            // section 2; 0x80..0xBF
            rgb.r = rampup_adj_with_floor;
            rgb.g = brightness_floor;
            rgb.b = rampdown_adj_with_floor;
        }
    } else {
        // section 0: 0x00..0x3F
        rgb.r = rampdown_adj_with_floor;
        rgb.g = rampup_adj_with_floor;
        rgb.b = brightness_floor;
    }
}

// fill_rainbow() from fastled's colorutils, but meant to work with 4bpp arrays
void fill_rainbow_smolboi( uint16_t * targetArray, int numToFill,
                  uint8_t initialhue,
                  uint8_t deltahue )
{
    CHSV hsv;
    hsv.hue = initialhue;
    hsv.val = 255;
    hsv.sat = 240;

    for( int i = 0; i < numToFill; ++i) {
        CRGB rgb;
        hsv2rgb_raw_C(hsv,rgb);
        leds[i] = M_Pack_4bpp(rgb.r >> 4, rgb.g >> 4, rgb.b >> 4);
        hsv.hue += deltahue;
    }
}

uint16_t rainbowTick() {
  uint8_t deltaHue = 10;
  uint8_t thisHue  = localStateVars[0]+=3;
  fill_rainbow_smolboi(leds, NUM_LEDS, thisHue, deltaHue);
  return 0;
}

#endif

//////////////////////////////////////////////////
// randomized functions
//////////////////////////////////////////////////

uint16_t randomFill( uint16_t(*f)() ) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = f();
  }
  return M_MsToTick(100);
}

#ifdef INCLUDE_CHAOS
uint16_t chaosFill() {
    uint8_t picked_number = random() & 7;
    return M_Get_Floodtable_Color(picked_number);
}

uint16_t chaosTick() {
  return randomFill(chaosFill);
}
#endif

#ifdef INCLUDE_SUPERCOMPUTER
uint16_t supercomputerFill() {
  return random() & 0xFF > 30 ? 0x000F : 0;
}

uint16_t supercomputerTick() {
  return randomFill(supercomputerFill);
}
#endif

//////////////////////////////////////////////////
// pacman chaser effect (sorry, no power pellets)
//////////////////////////////////////////////////

#ifdef INCLUDE_PACMAN

#ifndef HIGH_DENSITY
  #define PACMAN_SPACING 3
#else
  #define PACMAN_SPACING 4
#endif

int findDotInLeds(uint16_t value) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (leds[i] == value) return i;
  }
  return -1;
}


void pacmanDrawEntity(uint16_t color, int default_pos) {
  int pos = findDotInLeds(color);
  if (pos < 0) pos = default_pos;
  leds[pos] = 0;
  leds[(pos + 1) >= NUM_LEDS ? 0 : pos + 1] = color;
}

PROGMEM const uint16_t pacman_ghosts_table[] = {
  M_Pack_4bpp(0xF,0x6,0x0), // clyde
  M_Pack_4bpp(0,0xF,0xF), // inky
  M_Pack_4bpp(0xF,1,0xC), // pinky
  M_Pack_4bpp(0xF,0,0) // blinky
};

uint16_t pacmanTick() {
  const uint16_t pacman = M_Pack_4bpp(0xF,0xF,0);
  const uint16_t dot = M_Pack_4bpp(0x1,0x1,0x1);

  pacmanDrawEntity(pacman, 4*PACMAN_SPACING);
  for (int8_t i = 3; i >= 0; i--) {
    pacmanDrawEntity(pgm_read_word_near(&pacman_ghosts_table[i]), i*PACMAN_SPACING);
  }

  // draw dots ahead of pacman, making sure not to clobber the ghosts.
  int pacman_pos = findDotInLeds(pacman);
  for (int i = 0; i < NUM_LEDS; i+= 2) {
    if (i > pacman_pos && leds[i] == 0) leds[i] = dot;
  }

  return M_MsToTick(80);
}

#endif // #ifdef INCLUDE_PACMAN

//////////////////////////////////////////////////
// robotron 2084 scroller
// see https://www.youtube.com/watch?v=l800GL6NQPY
//////////////////////////////////////////////////
#ifdef INCLUDE_ROBOTRON

PROGMEM const uint16_t robotron_color_table[] = {
  M_Pack_4bpp(255>>5,255>>5,0),     // yellow
  M_Pack_4bpp(255>>5,96>>5,0),      // orange
  M_Pack_4bpp(0xF>>1,0,0xC>>1),           // pink
  M_Pack_4bpp(0,255>>5,0),          // green
  M_Pack_4bpp(255>>5,96>>5,0),      // orange
  M_Pack_4bpp(0,0,255>>5),          // blue
  M_Pack_4bpp(255>>5,0,0),          // red
};

uint16_t tinyModulo(uint16_t a, uint16_t b) {
  return a - b * (a/b);
}

uint16_t robotronTick() {
  if (localStateVars[0] < NUM_LEDS) {
    leds[localStateVars[0]++] = pgm_read_word_near(&robotron_color_table[tinyModulo(localStateVars[0],7)]);
    return M_MsToTick(80);
  }

  // on even frames, blink every 7th chaser counterclockwise
  int8_t dot = -1;
  if ((localStateTick & 1) != 0) {
    dot = 6 - tinyModulo((localStateTick >> 1), 7);
  }

  // advance the normal color chasers clockwise
  for (int i = 0; i < NUM_LEDS; i++) {
    if (dot != -1 && tinyModulo(i,7)==dot) leds[i] = 0x0FFF;
    else leds[i] = pgm_read_word_near(&robotron_color_table[ tinyModulo( abs(i - localStateTick), 7) ]);
  }

  localStateTick ++;
  return M_MsToTick(80);
}

#endif

//////////////////////////////////////////////////
// Main Arduino handlers and statemachine stuff
//////////////////////////////////////////////////

const PROGMEM bcs_tick_fcn_t handlers[NUMBER_OF_MODES] = {
#ifdef INCLUDE_CHASER
  chaserTick,
#endif
#ifdef INCLUDE_RAINBOW
  rainbowTick,
#endif
#ifdef INCLUDE_FLOOD
  floodTick,
#endif
#ifdef INCLUDE_CHAOS
  chaosTick,
#endif
#ifdef INCLUDE_HALF_FRAME_FLOOD
  halfFrameFloodTick,
#endif
#ifdef INCLUDE_PACMAN
  pacmanTick,
#endif
#ifdef INCLUDE_QUARTER_FRAME_FLOOD
  quarterFrameFloodTick,
#endif
#ifdef INCLUDE_SUPERCOMPUTER
  supercomputerTick,
#endif
#ifdef INCLUDE_ROBOTRON
  robotronTick,
#endif
};

/**
 * Reset local state
 */
void reset() {
  for (int i = 0; i < sizeof(globalStateVars); i++) {
    globalStateVars[i] = 0;
  }
  for (int i = 0; i < sizeof(localStateVars); i++) {
    localStateVars[i] = 0;
  }
  ledsReset();
}

void setup() {
  randomSeed(analogRead(3));
  reset();
}

void loop() {
  // we're trying to execute this at a (near) constant rate.
  // in all likelihood, we'll be executing slower than intended
  if ( millis() < loopTickNextOut) return;

  // advance RNG every tick
  random();

  unsigned long before = millis();

  // if the wait counter is ticking down, don't do anything for this tick,
  // otherwise call the appropriate handler
  if (waitForWaitCounterExpiry()) {
    bcs_tick_fcn_t f;
#ifdef USE_THIS_MODE_ONLY
    f = pgm_read_ptr_near( &handlers[USE_THIS_MODE_ONLY] );
#else
    f = pgm_read_ptr_near( &handlers[currentState] );
#endif
    waitCounter = f();

    // push LED updates after every tick executes
    // (saves code space)
    ledsSend();
  }

#ifndef USE_THIS_MODE_ONLY
  ticksElapsedThisState ++;
  if (ticksElapsedThisState > AUTOADVANCE_TICK_COUNT) {
    reset();
  
    currentState ++;
    if (currentState == NUMBER_OF_MODES) currentState = 0;
    ticksElapsedThisState = 0;
    
    return;
  }
#endif
  unsigned long loopTickDelta = millis() - before;
  loopTickNextOut = millis() + ((MS_PER_TICK >= loopTickDelta) ? (MS_PER_TICK - loopTickDelta) : 0);
}

