//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET control8 module ***=-
//
// The device sends MIDI commands over USB when knobs are adjusted
// Uses Arduino pro micro (atMega32U4), WS2812 led strips & 8 trimpots
// connected between microcontroller, VCC and GND.
// Latest version, details and build instructions is found on www.vonkonow.com
//
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//
// History: 2020-11-04
// * added startup LED animation
// * midichannel is stored in (non volatile) eeprom memory
// * improved midiOffset and midi-ch visualzation
// * cleaned up code
// History: 2020-11-24
// * adopted code from previous control project
//   (increased number of pots, removed keys)
// * added hysteresis to analog redings (trigAmount)

#include <MIDIUSB.h>
#include <NeoPixelBrightnessBus.h>
#include <EEPROM.h>

const bool debug = false;                         // <- serial debugging over USB
const bool echoLed = true;                        // <- echo LED without incoming MIDI
const bool startAnimation = true;                 // <- show start animation
const uint8_t trigAmount = 1;                     // <- change before sending update
const uint8_t ledBrightness = 42;                 // <- LED intensity (0-255)
const uint8_t noteIntensity = 64;                 // <- MIDI note volume (0-127)
const uint8_t maxAn = 8;                          // number of analog filters
const uint8_t anNum = 16;                         // amount of filter to hide noise
const uint8_t anPin[maxAn] = {A6, A7, A8, A9, A3, A2, A1, A0};
const uint8_t maxLeds = 8;
const uint8_t maxKeys = 3;
const uint8_t ledPin = 2;
const uint8_t keyModePin = 15;
const uint8_t keyOctUpPin = 16;
const uint8_t keyOctDownPin = 10;
const uint8_t keyMap[maxKeys] = {15, 16, 10};
const uint8_t ledMap[maxAn] = {3, 2, 1, 0, 4, 5, 6, 7};

// Runnig average analog filter:
uint8_t anPos;                                    // the index of the current reading
int anHistory[maxAn][anNum];                      // the lst readings from the analog input
int anOld[maxAn];                                 // analog old reading (detect change)
int anTotal[maxAn];                               // the running total
int anAvg[maxAn];                                 // analog average

uint8_t midiOffset = 0;                           // offset to allow several devices on same ch
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
  updateLEDs();                                   // Show knob value or fade midiOffset and midi ch LEDs
}

void checkKeys() {
  // get analog input (filtered)
  anUpdate();

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

  // midiOffset Up
  if (digitalRead(keyOctUpPin) == 0) {
    if (midiOffset < 9)
      midiOffset++;
    showmidiOffset();
    while (digitalRead(keyOctUpPin) == 0)
      delay(20);
  }

  // midiOffset Down
  if (digitalRead(keyOctDownPin) == 0) {
    if (midiOffset > 0)
      midiOffset--;
    showmidiOffset();
    while (digitalRead(keyOctDownPin) == 0)
      delay(20);
  }
}

void anUpdate() {
  for (uint8_t i = 0; i < maxAn; i++) {
    anTotal[i] = anTotal[i] - anHistory[i][anPos];  // subtract the last reading:
    anHistory[i][anPos] = analogRead(anPin[i]) / 8; // read from the sensor: 0-1023 => 0-127
    anTotal[i] = anTotal[i] + anHistory[i][anPos];  // add the reading to the anTotal:
  }
  anPos++;                                          // advance to the next position in the array:
  if (anPos >= anNum)                               // if we're at the end of the array...
    anPos = 0;                                      // ...wrap around to the beginning:
  for (uint8_t i = 0; i < maxAn; i++) {
    anAvg[i] = anTotal[i] / anNum;                  // calculate the average:
  }
}

void updateLEDs() {
  if (fadeLeds == true) {                           // fade status leds? (change chord length, midiOffset or midi)
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
    for (uint8_t i = 0; i < maxLeds; i++)
      strip.SetPixelColor(ledMap[i], wheelH(map(anAvg[i], 0, 127, 0, 654)));
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

void showmidiOffset() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= midiOffset)
      strip.SetPixelColor(ledMap[i], wheelH(map(i * 12, 0, 128, 0, 767)));
    else
      strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
    strip.Show();
  }
  fadeLeds = true;
}

void sendControl() {
  for (uint8_t i = 0; i < maxAn; i++) {
    if (anAvg[i] > anOld[i] + trigAmount || anAvg[i] < anOld[i] - trigAmount ) {
      anOld[i] = anAvg[i];
      MidiUSB.sendMIDI({0x0B, 0xB0 | midiCh , i + midiOffset * maxAn, anAvg[i]});
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
  MidiUSB.flush();
}

void darkenLED(uint8_t pos, uint8_t dark) {
  RgbColor tmpColor = strip.GetPixelColor(pos);
  tmpColor.Darken(dark);
  strip.SetPixelColor(pos, tmpColor);
}

void clearLED() {
  for (int i = 0; i < maxLeds; i++)
    strip.SetPixelColor(i, RgbColor(0, 0, 0));
  strip.Show();
}

void showLedAnimation() {
  for (int j = 0; j <= ((maxLeds + 10) * 10); j++) {    // all off once (17-7)/10=1
    for (int i = 0; i < maxLeds; i++) {
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
