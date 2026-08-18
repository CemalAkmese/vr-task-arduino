/// Arduino Script to interface with VR_Task, expects a flag (0,1) to start task and a Char to define task structure
// A and B: Corridor flags, will result in random tone with freq_a or freq_b where in corrdidor A tone A is rewarded left, tone B right,
// the opposite directions are rewarded in corridor B
// C and D: shaping trials for A and B, reward is delivered automatically  after tone is played
// R and L : no tone, R reward on right, L on Left for example if corridor and b are presented  A can be R, B can be L
// S and M: shaping trials for R and L
// T and O: only tone counts: T and O are for freq_a or freq_b
// G and N: Go and No Go Trials for one spout (right)

// after a trial, the outcome is sent back: dirtouch (1,2 for L,R) and taskoutcome 0,1,2,4 for FA,Hit,Miss,Correction
// import libraries
#include <Servo.h>
//#include <toneAC2.h>
#include <digitalWriteFast.h>

// declare variables

// state flags
int taskstate = 0; // start inactive
int active = 0; //active task state
int dirtouch = 0; //direction of touch
int servopos = 0; // remembres if servo is in or out
int isreward = 0; //if trial was rewarded
int randtone = 0; // tone is randomly chosen
int toneon = 0; //
int spouton = 0;
int taskoutcome = 0; //0: FA 1: Hit 2: Miss 
int toggleon = 0; // to switch between toggle states
int allowcorrection = 1;
int touchedother = 0;

// fixed positions
int servorest = 120; // servo position at rest
int servoval = servorest;// servops to drive servo more slowly
int servotask = 170; // servo front position
int lspoutstate; // left spout touch state
int rspoutstate; // right spout touch state
int trialend = 0; // signal end of trial to reset flags and send serial to VR


// lick counting
int lickThreshold = 3;     // number of licks needed to commit a choice
int lLickCount = 0;         // left spout lick counter
int rLickCount = 0;         // right spout lick counter
int prevLsense = LOW;       // previous left sensor state (for edge detection)
int prevRsense = LOW;       // previous right sensor state (for edge detection)

// tone settings
int freq_a = 4000;
int freq_b = 7000;
unsigned long cuedur = 500; // cue duration
unsigned long precuedur = 2000; //  cue duration if played prior to task
int freq = 500; // freq to be assigned


// sensor inputs
int lsense;  // right spout touch sensor input
int rsense; // right spout touch sensor input
int togglestate; // state of toggle button

// counters
unsigned long tasktime;
unsigned long responsetime;
unsigned long spouttime;
unsigned long rewardtime;
unsigned long currentmillis;
unsigned long toggletime = 0;
unsigned long noisetime;
unsigned long timeout;
// at the top, with your other variables
unsigned long lastLeftLickTime = 0;
unsigned long lastRightLickTime = 0;
unsigned long lickDebounce = 100; // ms between counted licks — tune to taste

// intervals
unsigned long respwin = 2000; // response window
unsigned long pretonewin = 1000; // pre cue delay
unsigned long posttonewin = 500; // post cue delay
unsigned long spoutopen = 100; // valve opening time
unsigned long servodeadtime = 150; // time in which servo moves in, exclude lick detection to avoid artefacts
unsigned long endtrialdur = 2000; // delay at end of trial, either to consume reward or as timeout
unsigned long rt  = 0; // report rt back to serial
unsigned long toggledeadtime  = 500; // disable button for this time after press
unsigned long noisedur = 500; // white noiese dur
unsigned long rewardcuedur = 0;// reward cue
// Change this value to adjust the penalty duration (in milliseconds)
unsigned long falseAlarmTimeout = 2000; // e.g., 2 seconds extra penalty
bool falseAlarmStarted = false; // tracks whether the non-blocking penalty timer is running
unsigned long falseAlarmPenaltyEnd = 0;
// servo
Servo spoutmotor;
unsigned long corridorEndTimer = 0;
unsigned long postCorridorBreak = 0; // <--- SET YOUR BREAK DURATION HERE (in milliseconds, e.g., 1500ms = 1.5s)
bool breakStarted = false;
//  channels

// inputs
int lcap = 30; // left lick sensor
int rcap = 45; // right lick sensor
int toggle = 4;
// outputs setup
int lspout = 2;// left solenoid, also goes to daq
int rspout = 40;// right solenoid, also goes to daq
// Due audio trigger: 2-bit state on these pins selects the sound the
// Due plays (see CustomTriggerMega.ino / CustomTriggerDue.ino truth table)
int duePin3 = 3; // -> Due pin 3
int duePin5 = 5; // -> Due pin 2 (via level shifter)
// second amp on 4
int ch_spoutmotor = 7; // servo
// outputs daq
int lsenseOut = 42;
int rsenseOut = 44;
int servoOut = 50;
//20 and 21 for i2c output

// trial event pulse codes -> behavior DAQ only (see EvtFunc())
int EVT_PIN = 46;
// 32-bit sync barcode -> both DAQs (see BarcodeFunc())
int BARCODE_PIN = 48;


//set up serial parsing

char buf[80];

// set up serial read

int readline(int readch, char *buffer, int len) {
  static int pos = 0;
  int rpos;    if (readch > 0) {
    switch (readch) {
      case '\r': // Ignore CR
        break;
      case '\n': // Return on new-line
        rpos = pos;
        pos = 0;  // Reset position index ready for next time
        return rpos;
      default:
        if (pos < len - 1) {
          buffer[pos++] = readch;
          buffer[pos] = 0;
        }
    }
  }
  return 0;
}

// Due audio trigger: duePin3/duePin5 encode a 2-bit state that tells the
// Due (running CustomTriggerDue.ino) which sound to play. See
// CustomTriggerMega.ino for the truth table this mirrors.
enum SoundState { SND_IDLE, SND_TONE_A, SND_TONE_B, SND_NOISE };

unsigned long soundOffTime = 0;
bool soundTimerActive = false;

void SetSoundState(SoundState s) {
  switch (s) {
    case SND_IDLE:
      digitalWriteFast(duePin3, HIGH);
      digitalWriteFast(duePin5, HIGH);
      break;
    case SND_TONE_A:
      digitalWriteFast(duePin3, LOW);
      digitalWriteFast(duePin5, HIGH);
      break;
    case SND_TONE_B:
      digitalWriteFast(duePin3, HIGH);
      digitalWriteFast(duePin5, LOW);
      break;
    case SND_NOISE:
      digitalWriteFast(duePin3, LOW);
      digitalWriteFast(duePin5, LOW);
      break;
  }
}

// starts a sound state and schedules a non-blocking return to idle after
// 'dur' ms; call SoundTimerFunc() every loop() for the revert to happen
void StartSound(SoundState s, unsigned long dur) {
  SetSoundState(s);
  soundOffTime = millis() + dur;
  soundTimerActive = true;
}

// call every loop(); reverts to idle once a StartSound() duration elapses
void SoundTimerFunc() {
  if (soundTimerActive && millis() >= soundOffTime) {
    SetSoundState(SND_IDLE);
    soundTimerActive = false;
  }
}

// plays a blocking burst of white noise for 'dur' ms, then returns to idle.
// used for the first wrong touch under correction==1, and for false alarms
void PlayWhiteNoiseBlocking(unsigned long dur) {
  SetSoundState(SND_NOISE);
  delay(dur);
  SetSoundState(SND_IDLE);
}

void WhiteNoise() {
  QueueEvent(EVT_WHITE_NOISE);
  PlayWhiteNoiseBlocking(noisedur);
}

// =============================================================
// EVT_PIN: trial event pulse codes -> behavior DAQ only
// Non-blocking pulse-count encoding: N x 5ms HIGH pulses with 5ms LOW
// gaps between them = event code N. Events are queued so two events
// firing close together never collide on the wire -- each code is
// sent to completion (plus a longer inter-code gap) before the next
// one starts.
// =============================================================

const int EVT_REWARD_RIGHT    = 1;
const int EVT_REWARD_LEFT     = 2;
const int EVT_REWARD_CENTRE   = 3; // defined for parity with other rigs; nothing in this task triggers it (no center spout)
const int EVT_WHITE_NOISE     = 4;
const int EVT_SERVO_MOVED     = 5; // servo extended to task position
const int EVT_SERVO_RETRACTED = 6;

const unsigned long evtPulseHigh = 5;  // ms
const unsigned long evtPulseLow  = 5;  // ms
const unsigned long evtCodeGap   = 20; // ms silence between two different codes

#define EVT_QUEUE_LEN 8
int evtQueue[EVT_QUEUE_LEN];
int evtQueueHead = 0;
int evtQueueTail = 0;
int evtQueueCount = 0;

bool evtActive = false;
bool evtPinHigh = false;
int evtPulsesLeft = 0;
unsigned long evtNextChange = 0;

void QueueEvent(int code) {
  if (code <= 0 || evtQueueCount >= EVT_QUEUE_LEN) return; // drop if full, shouldn't happen
  evtQueue[evtQueueTail] = code;
  evtQueueTail = (evtQueueTail + 1) % EVT_QUEUE_LEN;
  evtQueueCount++;
}

// call every loop(); pumps the queued event codes out on EVT_PIN
void EvtFunc() {
  unsigned long now = millis();

  if (!evtActive) {
    if (evtQueueCount > 0 && now >= evtNextChange) {
      evtPulsesLeft = evtQueue[evtQueueHead];
      evtQueueHead = (evtQueueHead + 1) % EVT_QUEUE_LEN;
      evtQueueCount--;
      evtActive = true;
      digitalWriteFast(EVT_PIN, HIGH);
      evtPinHigh = true;
      evtNextChange = now + evtPulseHigh;
    }
    return;
  }

  if (now < evtNextChange) return;

  if (evtPinHigh) {
    // end of a HIGH pulse
    digitalWriteFast(EVT_PIN, LOW);
    evtPinHigh = false;
    evtPulsesLeft--;
    if (evtPulsesLeft <= 0) {
      evtActive = false;
      evtNextChange = now + evtCodeGap; // hold off next code so codes don't run together
    } else {
      evtNextChange = now + evtPulseLow;
    }
  } else {
    // start of the next pulse in the same code
    digitalWriteFast(EVT_PIN, HIGH);
    evtPinHigh = true;
    evtNextChange = now + evtPulseHigh;
  }
}

// =============================================================
// BARCODE_PIN: 32-bit sync barcode -> both DAQs
// 20ms HIGH start pulse -> 20ms LOW gap -> 32 x 29ms data bits
// (LSB first) -> 30s wait, then repeat with an incrementing counter.
// Runs continuously regardless of task state.
// =============================================================

const unsigned long barcodeInterval  = 30000; // ms between barcodes
const unsigned long barcodeStartDur  = 20;    // ms
const unsigned long barcodeGapDur    = 20;    // ms
const unsigned long barcodeBitDur    = 29;    // ms per bit

enum BarcodePhase { BC_IDLE, BC_START, BC_GAP, BC_BIT };

unsigned long barcodeValue = 0; // increments once per barcode sent
BarcodePhase barcodePhase = BC_IDLE;
unsigned long barcodeCycleStart = 0;
unsigned long barcodePhaseEnd = 0;
int barcodeBitIndex = 0;

void BarcodeFunc() {
  unsigned long now = millis();

  switch (barcodePhase) {
    case BC_IDLE:
      if (now - barcodeCycleStart >= barcodeInterval) {
        digitalWriteFast(BARCODE_PIN, HIGH);
        barcodePhaseEnd = now + barcodeStartDur;
        barcodePhase = BC_START;
      }
      break;

    case BC_START:
      if (now >= barcodePhaseEnd) {
        digitalWriteFast(BARCODE_PIN, LOW);
        barcodePhaseEnd = now + barcodeGapDur;
        barcodePhase = BC_GAP;
      }
      break;

    case BC_GAP:
      if (now >= barcodePhaseEnd) {
        barcodeBitIndex = 0;
        digitalWriteFast(BARCODE_PIN, bitRead(barcodeValue, 0) ? HIGH : LOW);
        barcodePhaseEnd = now + barcodeBitDur;
        barcodePhase = BC_BIT;
      }
      break;

    case BC_BIT:
      if (now >= barcodePhaseEnd) {
        barcodeBitIndex++;
        if (barcodeBitIndex >= 32) {
          digitalWriteFast(BARCODE_PIN, LOW);
          barcodeValue++;
          barcodeCycleStart = now; // 30s wait starts from end of transmission
          barcodePhase = BC_IDLE;
        } else {
          digitalWriteFast(BARCODE_PIN, bitRead(barcodeValue, barcodeBitIndex) ? HIGH : LOW);
          barcodePhaseEnd = now + barcodeBitDur;
        }
      }
      break;
  }
}
void setup() {

  // attach servo
  Serial.begin(115200);
  spoutmotor.attach(ch_spoutmotor);
  spoutmotor.write(servorest);

  // declare input and output channels
  pinMode(lcap, INPUT);
  pinMode(lspout, OUTPUT);
  pinMode(rspout, OUTPUT);
  pinMode(duePin3, OUTPUT);
  pinMode(duePin5, OUTPUT);
  pinMode(lcap, INPUT);
  pinMode(EVT_PIN, OUTPUT);
  pinMode(BARCODE_PIN, OUTPUT);
  pinMode(servoOut, OUTPUT);
  pinMode(rsenseOut, OUTPUT);
  pinMode(lsenseOut, OUTPUT);
  pinMode(toggle, INPUT_PULLUP);
  SetSoundState(SND_IDLE); // Due starts up idle (both lines HIGH)
  digitalWriteFast(EVT_PIN, LOW);
  digitalWriteFast(BARCODE_PIN, LOW);
  barcodeCycleStart = millis(); // first sync barcode fires 30s after boot
  // random seed for tone choice
  randomSeed(analogRead(1));


}



// Main Loop
void loop() {

  // generate timestamp for loop
  currentmillis = millis();

  // read sensor input
  lsense = digitalReadFast(lcap); // always sample spout inputs in loop
  rsense = digitalReadFast(rcap);

  // if off read from serial and toggle button
  if (taskstate == 0) {
    SerialInfo(); // read from serial

    togglestate = digitalReadFast(toggle);
    if (togglestate == LOW) {

      if (currentmillis - toggledeadtime >= toggletime) {
        if (toggleon == 0) {
          toggletime = currentmillis;
          toggleon = 1;
          spoutmotor.write(servotask);
        }
      }

      if (currentmillis - toggledeadtime >= toggletime) {
        if (toggleon == 1) {
          toggletime = currentmillis;
          toggleon = 0;
          spoutmotor.write(servorest);

        }


      }


    }
  }
 if (taskstate == 1) {
    Serial.flush();
    }

  // switch all on if taskstate has been inactive, timestamp task start
  if (active == 0) {
    if (taskstate == 1) {
      active = 1; //start of task
      tasktime = currentmillis;
    }
  }


  // call task functions in each loop

  CueFunc();
  ServoFunc();
  LickDetection();
  GiveReward();
  EndReward();
  EndTask();
  FalseAlarm();
  NoResponse();
  SoundTimerFunc();
  EvtFunc();
  BarcodeFunc();

  // write out info
  WriteOut();
}


// Helper Functions


// plays or omits cue
void   CueFunc() {

  //start cue after delay
  if (active == 1) {
    if (toneon == 1) {
      if (currentmillis - pretonewin >= tasktime) {
        //toneAC2(speaker1, speaker2, freq, cuedur, true);
        if (cuedur > 0) {
          SetSoundState(freq == freq_b ? SND_TONE_B : SND_TONE_A);
        }
        toneon = 2; //record tone out to flag
        active = 2; // next task stage
      }
    }
    if (toneon == 0) {
      if (currentmillis - pretonewin >= tasktime) {
        // if no tone still progress to next taskstage
        active = 2; // next task stage

      }
    }
  }
  if (active == 2) {
    if (toneon == 2) {
      if (currentmillis - pretonewin - cuedur >= tasktime) {
        SetSoundState(SND_IDLE);
      }
    }
  }
}

// moves servo in and out depending on state of 'active' flags and delays
void ServoFunc() {
  if (active == 2) {
    if (servopos == 0) {
      // Check if the baseline pre-tone and cue conditions are met
      if (currentmillis - pretonewin - cuedur - posttonewin >= tasktime) {
        
        // 1. If the break hasn't started yet, capture the start timestamp
        if (!breakStarted) {
          corridorEndTimer = currentmillis;
          breakStarted = true;
        }

        // 2. Only proceed to move the servo once the post-corridor break has elapsed
        if (currentmillis - corridorEndTimer >= postCorridorBreak) {
          
          while (servoval < servotask) {
            servoval = servoval + 5;
            spoutmotor.write(servoval);
            digitalWriteFast(servoOut, HIGH);
            delay(25);
          }
          
          servopos = 1;
          active = 3;
          spouttime = currentmillis; // Start counting time that servo has moved in
          breakStarted = false;      // Reset the flag for the next trial
          QueueEvent(EVT_SERVO_MOVED);
        }
      }
    }
  }

  if (active == 3) {
    if (currentmillis - servodeadtime >= spouttime) {
      active = 4;
    }
  }
  
  if (active == 0) {
    if (servopos == 1) {
      spoutmotor.write(servorest);
      digitalWriteFast(servoOut, LOW);
      servopos = 0;
      servoval = servorest;
      breakStarted = false; // Ensure it's reset if a trial aborts
      QueueEvent(EVT_SERVO_RETRACTED);
    }
  }
}

// in response window check sensors

// Full replacement for LickDetection()
void LickDetection() {
  if (active == 4) {
    if (currentmillis - respwin <= spouttime) {

      // --- shaping trials: auto-reward, no lick counting needed ---
      if (dirtouch == 3) {
        active = 5;
        rt = currentmillis - spouttime;
      }
      if (dirtouch == 4) {
        active = 5;
        rt = currentmillis - spouttime;
      }

      // --- detect rising edges (individual lick events) ---
      bool leftLickEvent  = (lsense == HIGH && prevLsense == LOW);
      bool rightLickEvent = (rsense == HIGH && prevRsense == LOW);

      // update previous states
      prevLsense = lsense;
      prevRsense = rsense;

      // count lick events per spout WITH debounce
      if (leftLickEvent && (currentmillis - lastLeftLickTime >= lickDebounce)) {
        lLickCount++;
        lastLeftLickTime = currentmillis;
      }
      if (rightLickEvent && (currentmillis - lastRightLickTime >= lickDebounce)) {
        rLickCount++;
        lastRightLickTime = currentmillis;
      }

      // --- evaluate once a spout reaches threshold ---

      // LEFT spout reached threshold
      if (lLickCount >= lickThreshold) {
        if (dirtouch == 1) {
          // correct side
          active = 5;
          rt = currentmillis - spouttime;
        }
        if (dirtouch == 2) {
          // wrong side
          if (allowcorrection == 0) {
            active = 6;
          }
          if (allowcorrection == 1) {
            if (touchedother == 0) {
              touchedother = 1;
              lLickCount = 0;
              rLickCount = 0;
              WhiteNoise();
            }
          }
        }
      }

      // RIGHT spout reached threshold
      if (rLickCount >= lickThreshold) {
        if (dirtouch == 2) {
          // correct side
          active = 5;
          rt = currentmillis - spouttime;
        }
        if (dirtouch == 1) {
          // wrong side
          if (allowcorrection == 0) {
            active = 6;
          }
          if (allowcorrection == 1) {
            if (touchedother == 0) {
              touchedother = 1;
              lLickCount = 0;
              rLickCount = 0;
              WhiteNoise();
            }
          }
        }
      }
    }
  }
}

// miss trials end task after response window
void NoResponse() {
  // check if lick sensor in high during active task in response window, assign active to FA (5) or HIT (4)
  if (active == 4) {
    if (currentmillis - respwin >= spouttime) {

      trialend = 1;
      taskoutcome = 2;

      if   (touchedother ==1)  {
        taskoutcome = 0; // if wrong spout was touched and been waiting for correction, still return FA
      }  
    }
  }
}

// open solenoid and get timestamp
void GiveReward() {
  // deliver rewards
  if (active == 5) {

    if (dirtouch == 1 || dirtouch == 3) {
      digitalWriteFast(lspout, HIGH);
      rewardtime = currentmillis;
      if (rewardcuedur > 0) {
        StartSound(freq == freq_b ? SND_TONE_B : SND_TONE_A, rewardcuedur);
      }
      isreward = 1;
      taskoutcome = 1;
      spouton = 1;
      active = 7;
      QueueEvent(EVT_REWARD_LEFT);
    }
    if (dirtouch == 2 || dirtouch == 4) {
      digitalWriteFast(rspout, HIGH);
      rewardtime = currentmillis;
      if (rewardcuedur > 0) {
        StartSound(freq == freq_b ? SND_TONE_B : SND_TONE_A, rewardcuedur);
      }
      isreward = 1;
      taskoutcome = 1;
      spouton = 1;
      active = 7;
      QueueEvent(EVT_REWARD_RIGHT);
    }
  if (touchedother ==1){
    taskoutcome = 4; // if reward but touched other spout is true, return correction trial
  }
  }
}

// close reward spout after rewarded trial
void EndReward() {
  // close both solenoids by default
  if (isreward == 1) {
    if (spouton == 1) {

      if (currentmillis - spoutopen > rewardtime) {
        digitalWriteFast(rspout, LOW);
        digitalWriteFast(lspout, LOW);
        trialend = 1;
        spouton = 0;
      }
    }
  }
}


// false alarm ends task
// false alarm ends task with an added penalty delay
void FalseAlarm() {
  if (active == 6) {
    if (!falseAlarmStarted) {
      // 1. Retract the spouts immediately to remove access
      spoutmotor.write(servorest);

      // 2. Play white noise burst
      QueueEvent(EVT_WHITE_NOISE);
      PlayWhiteNoiseBlocking(noisedur);

      // 3. Start the additional False Alarm timeout penalty (non-blocking)
      falseAlarmPenaltyEnd = millis() + falseAlarmTimeout;
      falseAlarmStarted = true;
    }

    // 4. Once the penalty has elapsed, signal the architecture to progress
    // to the end and reset variables
    if (millis() >= falseAlarmPenaltyEnd) {
      trialend = 1;
      falseAlarmStarted = false; // reset for the next false alarm
    }
  }
}


// ending task and resetting flags
void EndTask() {
  if (trialend == 1) {
    timeout = currentmillis;
    trialend = 10;
    active = 10;
  }
  if (trialend == 10) {
    if (currentmillis - endtrialdur > timeout) {
    // wait before resetting variables just in case;
    // send back trial info
    Serial.print(dirtouch);//direction
    Serial.print(',');
    Serial.println(taskoutcome);//rewarded or not
    //      Serial.print(',');
    //      Serial.println(rt); // RT
    delay(1000); // wait a few ms before resetting variables just in case;

    // reset flags
    dirtouch = 0;
    active = 0;
    taskstate = 0;
    isreward = 0;
    trialend = 0;
    toneon = 0;
    freq = 0;
    taskoutcome = 0;
    cuedur  = 200; 
    // reset rt
    rt = 0;
    touchedother = 0;
    lLickCount = 0;
    rLickCount = 0;
    prevLsense = LOW;
    prevRsense = LOW;
    lastLeftLickTime = 0;
    lastRightLickTime = 0;
    falseAlarmStarted = false;
    }
  }
}

// check incoming serial info and activate task
void SerialInfo() {
  if (readline(Serial.read(), buf, 80 ) > 0) {
    // this is to test serial connection,
    if (strlen(buf) == 4) {
      Serial.println(buf);//send back to signal Arduino is ready
    }

    if (strlen(buf) == 6) {

      if (buf[1] == '2') {
        if (buf[4] == 'H') {
          StartSound(SND_TONE_B, precuedur);
        }
        if (buf[4] == 'T') {
          StartSound(SND_TONE_A, precuedur);
        }
      }
      if (buf[1] == '1') {

        taskstate = 1;

        if (buf[4] == 'A') {
          //A corridor, tone A rewarded left
          randtone = random(2);
          toneon = 1;
          if (randtone == 0) {
            freq = freq_a;
            dirtouch = 1;
          }
          if (randtone == 1) {
            freq = freq_b;
            dirtouch = 2;
          }
        }
        // parse input to decide task params

        if (buf[4] == 'B') {
          //B corridor, tone A rewarded right
          randtone = random(2);
          toneon = 1;
          if (randtone == 0) {
            freq = freq_b;
            dirtouch = 2;
          }
          if (randtone == 1) {
            freq = freq_a;
            dirtouch = 1;
          }
        }

        if (buf[4] == 'L') {
          //rewarded on left

          dirtouch = 1;
          cuedur = 0;
          pretonewin = 0;
        }
        if (buf[4] == 'R') {
          //rewarded on right

          dirtouch = 2;
          cuedur = 0;
          pretonewin = 0;
        }
        if (buf[4] == 'G') {
          //rewarded on right
          rewardcuedur = 500;
          dirtouch = 2;
          cuedur = 0;
          pretonewin = 0;
          freq = freq_b;
        }
        if (buf[4] == 'N') {
          //not rewarded on right

          dirtouch = 1;
          cuedur = 0;
          pretonewin = 0;
        }



        // shaping trials

        if (buf[4] == 'C') {
          //A corridor, tone A rewarded left
          randtone = random(2);
          toneon = 1;
          if (randtone == 0) {
            freq = freq_a;
            dirtouch = 3;
          }
          if (randtone == 1) {
            freq = freq_b;
            dirtouch = 4;
          }
        }
        if (buf[4] == 'D') {
          //B corridor, tone A rewarded right
          randtone = random(2);
          toneon = 1;
          if (randtone == 0) {
            freq = freq_b;
            dirtouch = 4;
          }
          if (randtone == 1) {
            freq = freq_a;
            dirtouch = 3;
          }
        }
        if (buf[4] == 'M') {
          //rewarded on left

          dirtouch = 3;
          cuedur = 0;
          pretonewin = 0;
        }
        if (buf[4] == 'S') {
          //rewarded on right

          dirtouch = 4;
          cuedur = 0;
          pretonewin = 0;
        }


        if (buf[4] == 'T') {
          // tone A rewarded left

          toneon = 1;

          freq = freq_a;
          dirtouch = 1;

        }
        if (buf[4] == 'O') {
          // tone B rewarded right

          toneon = 1;

          freq = freq_b;
          dirtouch = 2;

        }
      }
    }
   Serial.flush();
  }
}

// write outputs to daq
void    WriteOut() {
  digitalWriteFast(lsenseOut, lsense);
  digitalWriteFast(rsenseOut, rsense);

}
