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

// these are packed 4bpp ----gggg rrrrbbbb, there's simply not enough memory to store 8bpp
uint16_t leds[NUM_LEDS]; 

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

uint16_t prng;
uint16_t rngAdvance() {
  return ( prng = ((prng << 1) | ( prng & 0x8000 ? 1 : 0)) ^ ((analogRead(3) > analogRead(2) ? 0 : 1) ));
}
#define rand() rngAdvance()
#define srand(x) prng = x;

////////////////////////////////////////////////////////////////////////
// chaser effect
////////////////////////////////////////////////////////////////////////

#ifdef HIGH_DENSITY
  #define CHASER_STEP 4
#else
  #define CHASER_STEP 3
#endif

uint16_t chaserTick() {
  blankAllLeds();
  for (int i = localStateVars[0]; i < NUM_LEDS; i += CHASER_STEP) {
    leds[i] = M_Pack_4bpp(0xF,0xF,0xF);
  }
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


/**
 * divider: 0 = fill whole frame, 1 = fill halves, 2 = fill quarters.
 */
uint16_t floodTickCommon(uint8_t divider) {
  // if last and first LED blank, reset state machine
  if (leds[0] == 0 && leds[NUM_LEDS-1] == 0) {
    floodCurrentLed = 0;
    floodCurrentColor = 0;
  }

  leds[floodCurrentLed] = M_Get_Floodtable_Color(floodCurrentColor);
  if (divider > 0) {
    uint8_t step = NUM_LEDS >> divider;
    uint8_t curstep = step;
    for (int i = 1; i < (divider << 1); i++) {
      leds[curstep + floodCurrentLed] = leds[floodCurrentLed];
      curstep += step;
    }
  }
  // floodTickFill(divider,0,NUM_LEDS,floodCurrentLed,);
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
// rainbow
//////////////////////////////////////////////////////////////////////
#ifdef INCLUDE_RAINBOW

uint16_t calcRainbow(uint8_t step) {
  if (step < 0x10) {
    return M_Pack_4bpp(0x0F-step, step, 0);
  }
  step -= 0x10;
  if (step < 0x10) {
    return M_Pack_4bpp(0, 0x0F-step, step);
  }
  step -= 0x10;
  return M_Pack_4bpp(step, 0, 0x0F-step);
}

uint16_t rainbowTick() {
  uint8_t thisHue  = localStateVars[0];

  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = calcRainbow( thisHue ++ );
    if (thisHue == 0x30) thisHue = 0;
  }
  
  localStateVars[0] ++;
  if (localStateVars[0] == 0x30) localStateVars[0] = 0;

  return M_MsToTick(10);
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
    uint8_t picked_number;
    for (int tries = 0; tries < 4; tries++) {
      picked_number = rand() & 7;
      if (localStateVars[0] == picked_number) continue;
    }
    localStateVars[0] = picked_number;
    return M_Get_Floodtable_Color(picked_number);
}

uint16_t chaosTick() {
  return randomFill(chaosFill);
}
#endif

#ifdef INCLUDE_SUPERCOMPUTER
uint16_t supercomputerFill() {
  return rand() & 0xFF > 30 ? 0x000F : 0;
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


int pacmanDrawEntity(uint16_t color, int default_pos) {
  int pos = findDotInLeds(color);
  if (pos < 0) pos = default_pos;
  int newpos = (pos + 1) >= NUM_LEDS ? 0 : pos + 1;
  leds[pos] = 0;
  leds[newpos] = color;
  return newpos;
}

PROGMEM const uint16_t pacman_ghosts_table[] = {
  M_Pack_4bpp(0xF,0x6,0x0), // clyde
  M_Pack_4bpp(0,0xF,0xF), // inky
  M_Pack_4bpp(0xF,1,0xC), // pinky
  M_Pack_4bpp(0xF,0,0), // blinky
  M_Pack_4bpp(0xF,0xF,0), // pacman
};

uint16_t pacmanTick() {
  const uint16_t dot = M_Pack_4bpp(0x1,0x1,0x1);
  
  // last entity to be drawn is assumed to be pacman
  int lastent_pos = -1;
  for (int8_t i = 0; i < 5; i++) {
    lastent_pos = pacmanDrawEntity(pgm_read_word_near(&pacman_ghosts_table[i]), i*PACMAN_SPACING);
  }

  // draw dots ahead of pacman, making sure not to clobber the ghosts.
  for (int i = 0; i < NUM_LEDS; i+= 2) {
    if (i > lastent_pos && leds[i] == 0) leds[i] = dot;
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

// this method glitches out after so-and-so iterations.
// we do our best to hide it. the alternative is to use modulo,
// which works perfect, but takes more codespace
uint16_t robotronTick() {
  if (localStateVars[0] < NUM_LEDS) {
    leds[localStateVars[0]++] = pgm_read_word_near(&robotron_color_table[localStateVars[2]]);
    localStateVars[2]++;
    if (localStateVars[2] >= 7) {
      localStateVars[2] = 0;
    }
    return M_MsToTick(80);
  }

  // on even frames, blink every 7th chaser counterclockwise
  int8_t dot = -1;
  if (localStateVars[1]) {
    dot = 6 - localStateVars[2];
    localStateVars[2] ++;
    if (localStateVars[2] >= 7) localStateVars[2] = 0;
  }
  localStateVars[1] = localStateVars[1] ? 0 : 1;

  // advance the normal color chasers clockwise
  uint8_t step = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (step == dot) {
      leds[i] = 0x0FFF;
    } else {
      int8_t offs = abs(step-localStateVars[3]);
      while (offs >= 7) offs -= 7;
      leds[i] = pgm_read_word_near(&robotron_color_table[offs]);
    }
    step ++;
    if (step >= 7) step = 0;
  }

  localStateVars[3]++;
  if (localStateVars[3] >= 16*7) localStateVars[3] = 0;
  
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
  srand(analogRead(3));
  reset();
}

void loop() {
  // we're trying to execute this at a (near) constant rate.
  // in all likelihood, we'll be executing slower than intended
  if ( millis() < loopTickNextOut) return;

  // advance RNG every tick
  rand();

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

