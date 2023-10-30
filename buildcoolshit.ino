/**
 * Lightweight WS2812B LED driver for use with letter boards
 * 
 * This targets the ubiquitous letter boards you can get on Amazon or at your local dollar store
 * and the LED tape you can get easily off eBay/Aliexpress/etc.
 * 
 * Requires FastLED for legacy stuff such as rainbow calculation.
 * A variant on avr-ws2812 (https://github.com/stephendpmurphy/avr-ws2812) is used as the LED driver,
 * and has been modified to work on 4bpp data (saves a huge amount of work RAM).
 */

#include <FastLED.h>
#include "avr_ws2812.h"


// #define BIG_BOY for 12"x12" signs
// else we assume 6"x6"
#define BIG_BOY

// #define HIGH_DENSITY for higher-count LED strips
#define HIGH_DENSITY

#define LOW_DENSITY_LED_COUNT 6+6+6+6
#define HI_DENSITY_LED_COUNT 12+11+11+10
#define HI_DENSITY_BIG_BOY_LED_COUNT 18+17+17+16

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

////////////////////////////////////////////////////////////////////////
// typedefs
////////////////////////////////////////////////////////////////////////
typedef enum {
 CHASER = 0,
 RAINBOW,
 FLOOD,
 CHAOS,
 HALF_FRAME_FLOOD,
 PACMAN,
 QUARTER_FRAME_FLOOD,
 SUPERCOMPUTER,
 ROBOTRON,
 NUMBER_OF_MODES
} bcs_state_t;

typedef void(*bcs_tick_fcn_t)();

////////////////////////////////////////////////////////////////////////
// global vars
////////////////////////////////////////////////////////////////////////

uint8_t currentState;
uint16_t leds[NUM_LEDS];

unsigned long loopTickNextOut;
unsigned long ticksElapsedThisState;

uint16_t localStateTick;
uint16_t localWaitCounter;

void ledsReset() {
  blankAllLeds();
}

void ledsSend() {
  // prevent unwelcome data corruption
  uint8_t* ledptr = (uint8_t*)&leds[1]; // assumes little endianness
  for (int i = 0; i < NUM_LEDS; i++) {
    *ledptr &= 0x0F;
    ledptr += 2;
  }

  ws2812_setleds_4bpp( leds, NUM_LEDS );
}

int findDotInLeds(uint16_t value) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (leds[i] == value) return i;
  }
  return -1;
}

uint8_t inline waitForWaitCounterExpiry() {
  if (localWaitCounter != 0) {
    localWaitCounter --;
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

void chaserTick() {
  blankAllLeds();
  for (int i = localStateTick; i < NUM_LEDS; i += CHASER_STEP) M_SetWhite(leds,i);
  localStateTick ++;
  if (localStateTick > (CHASER_STEP-1)) localStateTick = 0;
  localWaitCounter = M_MsToTick(150);
}

//////////////////////////////////////////////////
// flood-fill schenanigans
//////////////////////////////////////////////////

uint8_t floodCurrentLed;
uint8_t floodCurrentColor;

PROGMEM const uint16_t g_flood_colors[] = {
  M_Pack_4bpp(255>>4,0,0),          // red
  M_Pack_4bpp(255>>4,96>>4,0),      // orange
  M_Pack_4bpp(255>>4,255>>4,0),     // yellow
  M_Pack_4bpp(0,255>>4,0),          // green
  M_Pack_4bpp(0,0,255>>4),          // blue
  M_Pack_4bpp(255>>4,0,255>>4),     // purple
  M_Pack_4bpp(0xF,0xF,0xF),         // white
  M_Pack_4bpp(0,0,0),               // off
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

/**
 * divider: 0 = fill whole frame, 1 = fill halves, 2 = fill quarters.
 */
void floodTickCommon(uint8_t divider) {
  // if last and first LED blank, reset state machine
  if (leds[0] == 0 && leds[NUM_LEDS-1] == 0x0000) {
    floodCurrentLed = 0;
    floodCurrentColor = 0;
  }

  uint16_t half = NUM_LEDS >> 1;
  uint16_t quarter = NUM_LEDS >> 2;

  if (floodCurrentLed == 0) {
    uint16_t c = M_Get_Floodtable_Color(floodCurrentColor);;
    leds[0] = c;
    if (divider >= 1) {
      leds[half] = c;
    }
    if (divider == 2) {
      leds[quarter] = c;
      leds[half + quarter] = c;
    }

    floodCurrentLed ++;
    floodCurrentColor ++;
    floodCurrentColor &= 7;
  } else {
    leds[floodCurrentLed] = leds[0];

    if (divider >= 1) {
      if ((floodCurrentLed + half) < NUM_LEDS) leds[floodCurrentLed + half] = leds[0];
    }
    if (divider == 2) {
      if ((floodCurrentLed + quarter) < NUM_LEDS) leds[floodCurrentLed + quarter] = leds[0];
      if ((floodCurrentLed + half + quarter) < NUM_LEDS) leds[floodCurrentLed + half + quarter] = leds[0];
    }

    floodCurrentLed ++;
    if (floodCurrentLed >= (NUM_LEDS >> divider)) {
      localWaitCounter = M_MsToTick(250);
      floodCurrentLed = 0;
    } else {
      localWaitCounter = (FLOOD_RATE << divider);
    }
  }
}

void floodTick() {
  floodTickCommon(0);
}

void halfFrameFloodTick() {
    floodTickCommon(1);
}

void quarterFrameFloodTick() {
    floodTickCommon(2);
}

//////////////////////////////////////////////////

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
        CRGB rgb = hsv;
        leds[i] = M_Pack_4bpp(rgb.r >> 4, rgb.g >> 4, rgb.b >> 4);
        hsv.hue += deltahue;
    }
}

void rainbowTick() {
  uint8_t deltaHue = 10;
  uint8_t thisHue  = beat8(100, localStateTick++);
  fill_rainbow_smolboi(leds, NUM_LEDS, thisHue, deltaHue);
}

//////////////////////////////////////////////////
// randomized functions
//////////////////////////////////////////////////

void chaosTick() {
  for (int i = 0; i < NUM_LEDS; i++) {
    long picked_number = random() & 7;
    leds[i] = M_Get_Floodtable_Color(picked_number);
  }
  localWaitCounter = M_MsToTick(100);
}

void supercomputerTick() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = random(0,100) > 30 ? 0x000F : 0;
  }
  localWaitCounter = M_MsToTick(100);
}

//////////////////////////////////////////////////
// pacman chaser effect (sorry, no power pellets)
//////////////////////////////////////////////////

#ifndef HIGH_DENSITY
  #define PACMAN_SPACING 3
#else
  #define PACMAN_SPACING 4
#endif

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

void pacmanTick() {
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

  localWaitCounter = M_MsToTick(80);
}

//////////////////////////////////////////////////
// robotron 2084 scroller
// see https://www.youtube.com/watch?v=l800GL6NQPY
//////////////////////////////////////////////////

PROGMEM const uint16_t robotron_color_table[] = {
  M_Pack_4bpp(255>>5,255>>5,0),     // yellow
  M_Pack_4bpp(255>>5,96>>5,0),      // orange
  M_Pack_4bpp(0xF>>1,1>>1,0xC>>1),           // pink
  M_Pack_4bpp(0,255>>5,0),          // green
  M_Pack_4bpp(255>>5,96>>5,0),      // orange
  M_Pack_4bpp(0,0,255>>5),          // blue
  M_Pack_4bpp(255>>5,0,0),          // red
};


void robotronTick() {
  // fill empty LEDs once per tick until all are set
  for (int i = 0; i < NUM_LEDS; i++) {
    if (leds[i] == 0) {
      leds[i] = pgm_read_word_near(&robotron_color_table[i % 7]);
      localWaitCounter = M_MsToTick(80);
      return;
    }
  }

  // on even frames, blink every 7th chaser counterclockwise
  int8_t dot = -1;
  if ((localStateTick & 1) != 0) {
    dot = 6 - ((localStateTick >> 1) % 7);
  }

  // advance the normal color chasers clockwise
  for (int i = 0; i < NUM_LEDS; i++) {
    if (dot != -1 && (i%7)==dot) leds[i] = 0x0FFF;
    else leds[i] = pgm_read_word_near(&robotron_color_table[ abs(i - localStateTick) % 7]);
  }

  localStateTick ++;
  localWaitCounter = M_MsToTick(80);
}

//////////////////////////////////////////////////
// Main Arduino handlers and statemachine stuff
//////////////////////////////////////////////////

bcs_tick_fcn_t handlers[NUMBER_OF_MODES] = {
  chaserTick,
  rainbowTick,
  floodTick,
  chaosTick,
  halfFrameFloodTick,
  pacmanTick,
  quarterFrameFloodTick,
  supercomputerTick,
  robotronTick,
};

/**
 * Reset local state
 */
void reset() {
  // local state must reset
  ticksElapsedThisState = 0;
  loopTickNextOut = 0;

  localStateTick = 0;
  localWaitCounter = 0;

  // reset LEDs
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
#ifdef USE_THIS_MODE_ONLY
    handlers[USE_THIS_MODE_ONLY]();
#else
    handlers[currentState]();
#endif
    // push LED updates after every tick executes
    // (saves code space)
    ledsSend();
  }

#ifndef USE_THIS_MODE_ONLY
  ticksElapsedThisState ++;
  if (ticksElapsedThisState > AUTOADVANCE_TICK_COUNT) goto advance;
#endif
  unsigned long loopTickDelta = millis() - before;
  loopTickNextOut = millis() + ((MS_PER_TICK >= loopTickDelta) ? (MS_PER_TICK - loopTickDelta) : 0);
  return;

advance:
  reset();
  
  currentState ++;
  if (currentState == NUMBER_OF_MODES) currentState = 0;
  ticksElapsedThisState = 0;
}
