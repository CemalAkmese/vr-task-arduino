// =============================================================
// Due side: reads the 2-bit level state from the Mega.
//
// Mega signal pins:
//   Mega pin 3 -> level shifter -> Due pin 3
//   Mega pin 5 -> level shifter -> Due pin 2
//
// Shared ground:
//   Mega GND -> level shifter GND -> Due GND
//
// Due audio:
//   DAC0 / DAC1 -> amplifier / speaker
//
// Truth table at the DUE:
//   Due pin 3 HIGH, Due pin 2 HIGH -> IDLE
//   Due pin 3 LOW,  Due pin 2 HIGH -> Tone A
//   Due pin 3 HIGH, Due pin 2 LOW  -> Tone B
//   Due pin 3 LOW,  Due pin 2 LOW  -> White noise
//
// =============================================================

#include <math.h>

// =============================================================
// INPUT PINS ON THE DUE
// =============================================================

const int PIN_3 = 3;
const int PIN_2 = 2;

// =============================================================
// DEBOUNCE
// =============================================================

const unsigned long DEBOUNCE_MS = 5;

// =============================================================
// SOUND PARAMETERS
// =============================================================

static const float TONE_A_FREQ_HZ = 12000.0f;
static const float TONE_B_FREQ_HZ = 6000.0f;

static const uint16_t TONE_AMPLITUDE = 1800;
static const uint16_t NOISE_AMPLITUDE = 1800;

// =============================================================
// SIGNAL STATES
// =============================================================

enum SignalState {
  IDLE,
  TONE_A,
  TONE_B,
  WHITE_NOISE
};

SignalState activeState = IDLE;
SignalState candidateState = IDLE;

unsigned long candidateSince = 0;

// =============================================================
// DDS / DAC ENGINE
// =============================================================

static const uint32_t FS_HZ = 96000;

static const uint32_t LUT_SIZE = 1024;
static const uint32_t LUT_BITS = 10;

static uint16_t sineLUT[LUT_SIZE];

static const uint32_t PHASE_FRAC_BITS = 32 - LUT_BITS;
static const uint32_t PHASE_FRAC_MASK =
  (1u << PHASE_FRAC_BITS) - 1u;

volatile float fs_actual = 96000.0f;

volatile uint32_t phaseAcc = 0;
volatile uint32_t phaseInc = 0;

// =============================================================
// ATTACK / RELEASE ENVELOPE
// =============================================================

static const uint32_t ENV_MS = 1;

uint32_t envSamples = 96;

volatile uint32_t envPos = 0;

// =============================================================
// AUDIO MODES
// =============================================================

enum AudioMode : uint8_t {
  MODE_NONE = 0,
  MODE_TONE = 1,
  MODE_TONE_RELEASE = 2,
  MODE_NOISE = 3,
  MODE_NOISE_RELEASE = 4
};

volatile AudioMode audioMode = MODE_NONE;

// =============================================================
// WHITE NOISE LFSR
// =============================================================

volatile uint32_t lfsr = 0xA5A5A5A5u;

static inline uint32_t lfsr_next() {

  uint32_t x = lfsr;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;

  lfsr = x;

  return x;
}

// =============================================================
// DAC FUNCTIONS
// =============================================================

static inline void dacWriteCh(uint8_t ch, uint16_t v12) {

  while ((DACC->DACC_ISR & DACC_ISR_TXRDY) == 0) {
  }

  DACC->DACC_CDR =
    ((uint32_t)(ch & 1) << 12) |
    (v12 & 0x0FFF);
}

static inline void writeDACsForce(
  uint16_t v0,
  uint16_t v1
) {

  dacWriteCh(0, v0);
  dacWriteCh(1, v1);
}

// =============================================================
// FREQUENCY -> DDS PHASE INCREMENT
// =============================================================

static inline uint32_t freqToPhaseInc(float f_hz) {

  double x =
    (double)f_hz *
    4294967296.0 /
    (double)fs_actual;

  if (x < 0)
    x = 0;

  if (x > 4294967295.0)
    x = 4294967295.0;

  return (uint32_t)(x + 0.5);
}

// =============================================================
// ENVELOPE
// =============================================================

static inline int32_t envGainQ15(bool rising) {

  uint32_t pos =
    (envPos < envSamples)
    ? envPos
    : envSamples;

  int32_t g =
    (int32_t)(
      ((uint64_t)pos * 32768ULL)
      / envSamples
    );

  return rising ? g : (32768 - g);
}

// =============================================================
// APPLY ENVELOPE
// =============================================================

static inline uint16_t applyGain(
  uint16_t centeredSample12b,
  int32_t gainQ15
) {

  int32_t s =
    (int32_t)centeredSample12b - 2048;

  s =
    (s * gainQ15) >> 15;

  int32_t y =
    s + 2048;

  if (y < 0)
    y = 0;

  if (y > 4095)
    y = 4095;

  return (uint16_t)y;
}

// =============================================================
// SINE SAMPLE
// =============================================================

static inline uint16_t sampleSine() {

  phaseAcc += phaseInc;

  uint32_t idx =
    phaseAcc >> PHASE_FRAC_BITS;

  uint32_t frac =
    phaseAcc & PHASE_FRAC_MASK;

  uint16_t a =
    sineLUT[idx];

  uint16_t b =
    sineLUT[
      (idx + 1) &
      (LUT_SIZE - 1)
    ];

  int32_t diff =
    (int32_t)b - (int32_t)a;

  return (uint16_t)(
    (int32_t)a +
    (int32_t)(
      (
        (int64_t)diff *
        (int64_t)frac
      ) >>
      PHASE_FRAC_BITS
    )
  );
}

// =============================================================
// WHITE NOISE SAMPLE
// =============================================================

static inline uint16_t sampleNoise() {

  int32_t n =
    (int32_t)(lfsr_next() >> 20)
    - 2048;

  int32_t v =
    2048 +
    (n * (int32_t)NOISE_AMPLITUDE)
    / 2048;

  if (v < 0)
    v = 0;

  if (v > 4095)
    v = 4095;

  return (uint16_t)v;
}

// =============================================================
// 96 kHz TIMER INTERRUPT
// =============================================================

void TC0_Handler() {

  TC_GetStatus(TC0, 0);

  switch (audioMode) {

    // ---------------------------------------------------------
    // TONE
    // ---------------------------------------------------------

    case MODE_TONE: {

      uint16_t s =
        sampleSine();

      s =
        applyGain(
          s,
          envGainQ15(true)
        );

      dacWriteCh(0, s);
      dacWriteCh(1, s);

      if (envPos < envSamples)
        envPos++;

      return;
    }

    // ---------------------------------------------------------
    // TONE RELEASE
    // ---------------------------------------------------------

    case MODE_TONE_RELEASE: {

      uint16_t s =
        sampleSine();

      s =
        applyGain(
          s,
          envGainQ15(false)
        );

      dacWriteCh(0, s);
      dacWriteCh(1, s);

      envPos++;

      if (envPos >= envSamples) {

        audioMode = MODE_NONE;

        writeDACsForce(
          2048,
          2048
        );
      }

      return;
    }

    // ---------------------------------------------------------
    // WHITE NOISE
    // ---------------------------------------------------------

    case MODE_NOISE: {

      uint16_t s =
        applyGain(
          sampleNoise(),
          envGainQ15(true)
        );

      dacWriteCh(0, s);
      dacWriteCh(1, s);

      if (envPos < envSamples)
        envPos++;

      return;
    }

    // ---------------------------------------------------------
    // WHITE NOISE RELEASE
    // ---------------------------------------------------------

    case MODE_NOISE_RELEASE: {

      uint16_t s =
        applyGain(
          sampleNoise(),
          envGainQ15(false)
        );

      dacWriteCh(0, s);
      dacWriteCh(1, s);

      envPos++;

      if (envPos >= envSamples) {

        audioMode = MODE_NONE;

        writeDACsForce(
          2048,
          2048
        );
      }

      return;
    }

    // ---------------------------------------------------------
    // IDLE
    // ---------------------------------------------------------

    case MODE_NONE:

    default:

      return;
  }
}

// =============================================================
// TIMER SETUP
// =============================================================

void setupTimer96k() {

  pmc_set_writeprotect(false);

  pmc_enable_periph_clk(ID_TC0);

  TC_Configure(
    TC0,
    0,

    TC_CMR_TCCLKS_TIMER_CLOCK1 |
    TC_CMR_WAVE |
    TC_CMR_WAVSEL_UP_RC
  );

  const uint32_t tc_clk =
    42000000UL;

  const uint32_t rc =
    (tc_clk + FS_HZ / 2)
    / FS_HZ;

  TC_SetRC(
    TC0,
    0,
    rc
  );

  fs_actual =
    (float)tc_clk /
    (float)rc;

  TC0->TC_CHANNEL[0].TC_IER =
    TC_IER_CPCS;

  TC0->TC_CHANNEL[0].TC_IDR =
    ~TC_IER_CPCS;

  NVIC_EnableIRQ(TC0_IRQn);

  TC_Start(
    TC0,
    0
  );
}

// =============================================================
// BUILD SINE TABLE
// =============================================================

void buildSineLUT() {

  for (
    uint32_t i = 0;
    i < LUT_SIZE;
    i++
  ) {

    float x =
      sinf(
        2.0f *
        3.14159265f *
        (float)i /
        (float)LUT_SIZE
      );

    int v =
      2048 +
      (int)(
        TONE_AMPLITUDE * x
      );

    if (v < 0)
      v = 0;

    if (v > 4095)
      v = 4095;

    sineLUT[i] =
      (uint16_t)v;
  }
}

// =============================================================
// START TONE A
// =============================================================

void startToneA() {

  phaseAcc = 0;

  phaseInc =
    freqToPhaseInc(
      TONE_A_FREQ_HZ
    );

  envPos = 0;

  audioMode =
    MODE_TONE;
}

// =============================================================
// START TONE B
// =============================================================

void startToneB() {

  phaseAcc = 0;

  phaseInc =
    freqToPhaseInc(
      TONE_B_FREQ_HZ
    );

  envPos = 0;

  audioMode =
    MODE_TONE;
}

// =============================================================
// START WHITE NOISE
// =============================================================

void startWhiteNoise() {

  envPos = 0;

  audioMode =
    MODE_NOISE;
}

// =============================================================
// STOP SOUND
// =============================================================

void stopSound() {

  if (audioMode == MODE_TONE) {

    envPos = 0;

    audioMode =
      MODE_TONE_RELEASE;
  }

  else if (audioMode == MODE_NOISE) {

    envPos = 0;

    audioMode =
      MODE_NOISE_RELEASE;
  }

  else {

    return;
  }

  while (
    audioMode == MODE_TONE_RELEASE ||
    audioMode == MODE_NOISE_RELEASE
  ) {

    // Wait for fade-out.
  }
}

// =============================================================
// READ THE TWO SIGNAL PINS
//
// IMPORTANT:
//
// Mega pin 3 -> Due pin 3
// Mega pin 5 -> Due pin 2
//
// The Due doesn't care that the Mega source is pin 5.
// It only sees the signal arriving on Due pin 2.
// =============================================================

SignalState readState() {

  bool p3 =
    digitalRead(PIN_3);

  bool p2 =
    digitalRead(PIN_2);

  if (
    p3 == HIGH &&
    p2 == HIGH
  )
    return IDLE;

  if (
    p3 == LOW &&
    p2 == HIGH
  )
    return TONE_A;

  if (
    p3 == HIGH &&
    p2 == LOW
  )
    return TONE_B;

  return WHITE_NOISE;
}

// =============================================================
// SETUP
// =============================================================

void setup() {

  Serial.begin(115200);

  // Due listens on its own pins 3 and 2.
  pinMode(
    PIN_3,
    INPUT_PULLUP
  );

  pinMode(
    PIN_2,
    INPUT_PULLUP
  );

  // -----------------------------------------------------------
  // DAC
  // -----------------------------------------------------------

  pmc_enable_periph_clk(ID_DACC);

  DACC->DACC_CR =
    DACC_CR_SWRST;

  DACC->DACC_MR =
    DACC_MR_TRGEN_DIS |
    DACC_MR_WORD_HALF |
    DACC_MR_TAG_EN;

  DACC->DACC_CHER =
    DACC_CHER_CH0 |
    DACC_CHER_CH1;

  // -----------------------------------------------------------
  // SINE TABLE
  // -----------------------------------------------------------

  buildSineLUT();

  // Silent at boot
  writeDACsForce(
    2048,
    2048
  );

  // -----------------------------------------------------------
  // TIMER
  // -----------------------------------------------------------

  setupTimer96k();

  envSamples =
    (uint32_t)(
      (uint64_t)(
        fs_actual + 0.5f
      ) *
      ENV_MS /
      1000ULL
    );

  if (envSamples < 1)
    envSamples = 1;

  Serial.println(
    "Ready. Waiting for signal on Due pins 2/3."
  );
}

// =============================================================
// MAIN LOOP
// =============================================================

void loop() {

  SignalState seen =
    readState();

  // Detect a new state
  if (seen != candidateState) {

    candidateState =
      seen;

    candidateSince =
      millis();
  }

  // Wait for stable state
  if (
    candidateState != activeState &&
    (millis() - candidateSince) >=
      DEBOUNCE_MS
  ) {

    // Stop previous sound
    if (activeState != IDLE)
      stopSound();

    activeState =
      candidateState;

    switch (activeState) {

      case TONE_A:

        startToneA();

        Serial.println(
          "Tone A (high pitch) triggered."
        );

        break;

      case TONE_B:

        startToneB();

        Serial.println(
          "Tone B (low pitch) triggered."
        );

        break;

      case WHITE_NOISE:

        startWhiteNoise();

        Serial.println(
          "White noise triggered."
        );

        break;

      case IDLE:

        Serial.println(
          "Back to idle/waiting."
        );

        break;
    }
  }
}