//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET control module ***=-
//
// The device sends MIDI commands over USB when knobs or keys are adjusted
// Uses Arduino pro micro (atMega32U4), WS2812 led strips & 7 keys
// connected between microcontroller and GND (using weak pull-ups).
// Latest version, details and build instructions is found on www.vonkonow.com
//
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//
// Future improvements:
// * improve how midi-ch and octaves are visualized (if > 4)
//
// History: 2020-11-04
// * added startup LED animation
// * midichannel is stored in (non volatile) eeprom memory
// * improved octave and midi-ch visualzation
// * cleaned up code

#include <MIDIUSB.h>
#include <NeoPixelBrightnessBus.h>
#include <EEPROM.h>

const bool debug = false;                         // <- serial debugging over USB
const bool echoLed = true;                        // <- echo LED without incoming MIDI
const bool startAnimation = true;                 // <- show start animation
const uint8_t ledBrightness = 42;                 // <- LED intensity (0-255)
const uint8_t noteIntensity = 64;                 // <- MIDI note volume (0-127)
const uint8_t anIn = 4;                           // number of analog filters
const uint8_t anNum = 16;                         // amount of filter to hide noise
const uint8_t anPin[anIn] = {A9, A8, A7, A6};     // analog inputs
const uint8_t maxLeds = 4;
const uint8_t maxNotes = 4;
const uint8_t maxKeys = 7;
const uint8_t ledPin = 2;
const uint8_t pot1 = A9;
const uint8_t pot2 = A8;
const uint8_t pot3 = A7;
const uint8_t pot4 = A6;
const uint8_t keyModePin = 19;
const uint8_t keyOctUpPin = 18;
const uint8_t keyOctDownPin = 10;
const uint8_t keyMap[maxKeys] = {20, 16, 15, 14, 19, 18, 10};
const uint8_t ledMap[maxNotes] = {3, 2, 1, 0};

// Runnig average analog filter:
uint8_t anPos;                                    // the index of the current reading
int anHistory[anIn][anNum];                       // the lst readings from the analog input
int anOld[anIn];                                  // analog old reading (detect change)
int anTotal[anIn];                                // the running total
int anAvg[anIn];                                  // analog average

uint16_t pressedKeys = 0x00;
uint16_t previousKeys = 0x00;
uint8_t octave = 0;                               // offset to midi control commands
uint8_t midiCh;                                   // loaded from EEPROM at startup
bool fadeLeds = false;


NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(maxLeds, ledPin);

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|

void setup() {
  for (int i = 0; i < maxKeys; i++)
    pinMode(keyMap[i], INPUT_PULLUP);
  if (debug == true) {
    Serial.begin(115200);
    Serial.println("Serial debug enabled: ");
  }

  midiCh = EEPROM.read(0);
  if (midiCh > 15) {                              // if default eeprom value is outside ch range
    midiCh = 0;
    EEPROM.update(0, midiCh);
  }

  strip.Begin();
  strip.SetBrightness(ledBrightness);
  if (startAnimation == true)
    showLedAnimation();
}

void loop() {
  checkKeys();                                    // user input (including knobs)
  sendControl();                                  // send control messages if user input
  updateLEDs();                                   // Show knob value or fade octave and midi ch LEDs
}

void checkKeys() {
  // get analog input (filtered)
  anUpdate();
  
  // check notes
  for (int i = 0; i < maxNotes; i++) {
    if (digitalRead(keyMap[i]) == LOW) {
      bitWrite(pressedKeys, i, 1);
    } else
      bitWrite(pressedKeys, i, 0);
  }
  
  // check midi channel
  if (digitalRead(keyModePin) == 0) {
    showMidi();
    while (digitalRead(keyModePin) == 0) {
      // midi up?
      if (digitalRead(keyOctUpPin) == 0) {
        if (midiCh < 15)
          midiCh++;
        showMidi();
        EEPROM.update(0, midiCh);
        while (digitalRead(keyOctUpPin) == 0)
          delay(20);
      }
      // midi down?
      if (digitalRead(keyOctDownPin) == 0) {
        if (midiCh > 0)
          midiCh--;
        showMidi();
        EEPROM.update(0, midiCh);
        while (digitalRead(keyOctDownPin) == 0)
          delay(20);
      }
      delay(20);
    }
  }
  
  // Octave Up
  if (digitalRead(keyOctUpPin) == 0) {
    if (octave < 9)
      octave++;
    showOctave();
    while (digitalRead(keyOctUpPin) == 0)
      delay(20);
  }
  
  // Octave Down
  if (digitalRead(keyOctDownPin) == 0) {
    if (octave > 0)
      octave--;
    showOctave();
    while (digitalRead(keyOctDownPin) == 0)
      delay(20);
  }
}

void anUpdate() {
  for (uint8_t i = 0; i < anIn; i++) {
    anTotal[i] = anTotal[i] - anHistory[i][anPos];  // subtract the last reading:
    anHistory[i][anPos] = analogRead(anPin[i]) / 8; // read from the sensor: 0-1023 => 0-127
    anTotal[i] = anTotal[i] + anHistory[i][anPos];  // add the reading to the anTotal:
  }
  anPos++;                                          // advance to the next position in the array:
  if (anPos >= anNum)                               // if we're at the end of the array...
    anPos = 0;                                      // ...wrap around to the beginning:
  for (uint8_t i = 0; i < anIn; i++) {
    anAvg[i] = anTotal[i] / anNum;                  // calculate the average:
  }
}

void updateLEDs() {
  if (fadeLeds == true) {                           // fade status leds? (change chord length, octave or midi)
    fadeLeds = false;
    for (uint8_t i = 0; i < maxLeds; i++) {
      RgbColor tmpColor = strip.GetPixelColor(i);
      tmpColor.Darken(1);
      if (tmpColor != RgbColor(0, 0, 0))
        fadeLeds = true;
      strip.SetPixelColor(i, tmpColor);
    }
    delay(10);
    strip.Show();
  } else {
    strip.SetPixelColor(3, wheelH(map(anAvg[0], 0, 127, 0, 654)));
    strip.SetPixelColor(2, wheelH(map(anAvg[1], 0, 127, 0, 654)));
    strip.SetPixelColor(1, wheelH(map(anAvg[2], 0, 127, 0, 654)));
    strip.SetPixelColor(0, wheelH(map(anAvg[3], 0, 127, 0, 654)));
  }
  strip.Show();
}

void showMidi() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= midiCh)
      strip.SetPixelColor(ledMap[i], wheelH(map(i, 0, 16, 0, 767)));
    else
      strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
    strip.Show();
  }
  fadeLeds = true;
}

void showOctave() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= octave)
      strip.SetPixelColor(ledMap[i], wheelH(map(i * 12, 0, 128, 0, 767)));
    else
      strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
    strip.Show();
  }
  fadeLeds = true;
}

void sendControl() {
  for (uint8_t i = 0; i < anIn; i++) {
    if (anAvg[i] != anOld[i]) {
      anOld[i] = anAvg[i];
      MidiUSB.sendMIDI({0x0B, 0xB0 | midiCh , i + octave * anIn, anAvg[i]});
      if (debug == true) {
        Serial.print("CtrlChange Ch:");
        Serial.print(midiCh);
        Serial.print(" Controller:");
        Serial.print(i, HEX);
        Serial.print(" Value:");
        Serial.println(anAvg[i], HEX);
      }
    }
  }
  for (int i = 0; i < maxNotes; i++) {
    if (bitRead(pressedKeys, i) != bitRead(previousKeys, i)) {
      if (bitRead(pressedKeys, i)) {
        bitWrite(previousKeys, i , 1);
        MidiUSB.sendMIDI({0x0B, 0xB0 | midiCh , i + octave * (anIn + 1), noteIntensity});
        if (debug == true) {
          Serial.print("CtrlChange On Ch:");
          Serial.print(midiCh);
          Serial.print(" Controller:");
          Serial.println(i + 4, HEX);
        }
      } else {
        bitWrite(previousKeys, i , 0);
        MidiUSB.sendMIDI({0x0B, 0xB0 | midiCh , i + octave * (anIn + 1), 0});
        if (debug == true) {
          Serial.print("CtrlChange Off Ch:");
          Serial.print(midiCh);
          Serial.print(" Controller:");
          Serial.println(i + 4, HEX);
        }
      }
    }
  }
  MidiUSB.flush();
}

void darkenLED(uint8_t pos, uint8_t dark) {
  RgbColor tmpColor = strip.GetPixelColor(pos);
  tmpColor.Darken(dark);
  strip.SetPixelColor(pos, tmpColor);
}

void clearLED() {
  for (int i = 0; i < maxLeds; i++) {
    strip.SetPixelColor(i, RgbColor(0, 0, 0));
  }
  strip.Show();
}

void showLedAnimation() {
  for (int j = 0; j <= 14; j++) {        // all off once (17-7)/10=1
    for (int i = 0; i < 4; i++) {
      float intensity = (float(j / 10) - i) / 10;
      if (intensity < 0) intensity = 1;   // wait until your turn
      if (intensity > 1) intensity = 1;   // stay off
      strip.SetPixelColor(i, RgbColor::LinearBlend(wheelH(i * 90), RgbColor(0, 0, 0), intensity));
    }
    strip.Show();
    delay(5);
  }
}

// Input a value 0 to 767 to get corresponding rainbow color
RgbColor wheelH(int wheelPos) {
  while ( wheelPos < 0)
    wheelPos += 768;
  while ( wheelPos >= 768)
    wheelPos -= 768;
  if (wheelPos >= 512)
    return RgbColor(wheelPos - 512, 0, 767 - wheelPos);
  if (wheelPos >= 256)
    return RgbColor(0, 511 - wheelPos, wheelPos - 256);
  return RgbColor(255 - wheelPos, wheelPos, 0);
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
