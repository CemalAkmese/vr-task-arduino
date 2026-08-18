// =============================================================
// Mega side: encodes which sound to play as a 2-bit level state
// on pins 3 and 5 (not a pulse -- the Due reads the live level).
//
// Wiring (through a logic level shifter, Mega 5V -> Due 3.3V):
//   Mega pin 3 -> level shifter -> Due pin 3
//   Mega pin 5 -> level shifter -> Due pin 5
//   Mega GND   -----------------> Due GND (shared ground)
//
// Truth table (pin3, pin5):
//   HIGH, HIGH -> idle / waiting for next signal
//   LOW,  HIGH -> Tone A (high pitch)
//   HIGH, LOW  -> Tone B (low pitch)
//   LOW,  LOW  -> White noise
// =============================================================

const int PIN_3 = 3;
const int PIN_5 = 5;

void setup() {

  Serial.begin(38400);

  pinMode(PIN_3, OUTPUT);
  pinMode(PIN_5, OUTPUT);

  go_idle();

  Serial.println(
    "Ready. Call play_tone_a() / play_tone_b() / play_white_noise()."
  );
}

// =============================================================
// Idle / waiting state: both lines HIGH.
// =============================================================

void go_idle() {

  digitalWrite(PIN_3, HIGH);
  digitalWrite(PIN_5, HIGH);
}

// =============================================================
// Tone A (high pitch):
// pin3 LOW, pin5 HIGH
// Held for duration_ms, then back to idle.
// =============================================================

void play_tone_a(unsigned long duration_ms) {

  digitalWrite(PIN_3, LOW);
  digitalWrite(PIN_5, HIGH);

  Serial.println(
    "Tone A signaled (pin3=LOW, pin5=HIGH)."
  );

  delay(duration_ms);

  go_idle();
}

// =============================================================
// Tone B (low pitch):
// pin3 HIGH, pin5 LOW
// Held for duration_ms, then back to idle.
// =============================================================

void play_tone_b(unsigned long duration_ms) {

  digitalWrite(PIN_3, HIGH);
  digitalWrite(PIN_5, LOW);

  Serial.println(
    "Tone B signaled (pin3=HIGH, pin5=LOW)."
  );

  delay(duration_ms);

  go_idle();
}

// =============================================================
// White noise:
// pin3 LOW, pin5 LOW
// Held for duration_ms, then back to idle.
//
// Both pins change when going from idle to white noise.
// The Due uses debounce to prevent a brief intermediate state.
// =============================================================

void play_white_noise(unsigned long duration_ms) {

  digitalWrite(PIN_3, LOW);
  digitalWrite(PIN_5, LOW);

  Serial.println(
    "White noise signaled (pin3=LOW, pin5=LOW)."
  );

  delay(duration_ms);

  go_idle();
}

// =============================================================
// TEST LOOP
//
// Tone A -> 2 seconds
// Idle    -> 1 second
// Tone B -> 2 seconds
// Idle    -> 1 second
// White noise -> 2 seconds
// Idle    -> 1 second
// Repeat
// =============================================================

void loop() {

  play_tone_a(2000);
  delay(1000);

  play_tone_b(2000);
  delay(1000);

  play_white_noise(2000);
  delay(1000);
}