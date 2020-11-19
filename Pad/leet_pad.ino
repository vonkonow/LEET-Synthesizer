//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET Pad module ***=-
//
// The device sends MIDI commands over USB when note keys are pressed
// (supports simultaneous notes, without ghosting).
// Uses Arduino pro micro (atMega32U4), WS2812 led strips & 11 keys
// connected between microcontroller and GND (using weak pull-ups).
// Latest version, details and build instructions is found on www.vonkonow.com
//
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//
// History: 2020-11-03
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
const uint8_t maxNoteKeys = 8;
const uint8_t maxKeys = 11;
const uint8_t maxLeds = 8;
const uint8_t ledPin = 2;
const uint8_t keyModePin = 19;
const uint8_t keyOctUpPin = 18;
const uint8_t keyOctDownPin = 10;
const uint8_t keyMap[maxKeys] = {5, 21, 15, 20, 16, 7, 14, 6, 19, 18, 10};
const uint8_t noteLedMap[maxLeds] = {3, 4, 2, 5, 1, 6, 0, 7};
const uint8_t statusLedMap[maxLeds] = {3, 2, 1, 0, 4, 5, 6, 7};

uint8_t octave = 4;                               // <- start octave
uint8_t midiCh;                                   // loaded from EEPROM at startup
uint16_t pressedNoteKeys = 0x00;
uint16_t previousNoteKeys = 0x00;
bool fadeLeds = false;

NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(maxLeds, ledPin);

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|

void setup() {
  if (debug == true) {
    Serial.begin(115200);
    Serial.print("Serial debug enabled: ");
  }

  for (int i = 0; i < maxKeys; i++)
    pinMode(keyMap[i], INPUT_PULLUP);

  midiCh = EEPROM.read(0);
  if (midiCh > 15) {
    midiCh = 0;
    EEPROM.update(0, midiCh);
  }

  strip.Begin();
  strip.SetBrightness(ledBrightness);
  if (startAnimation == true)
    showLedAnimation();
}

void loop() {
  checkKeys();
  playNotes();
  checkMidi();
  if (fadeLeds == true)
    fadeStatusLeds();
}

void checkKeys()
{
  // check notes
  for (int i = 0; i < maxNoteKeys; i++) {
    if (digitalRead(keyMap[i]) == LOW) {
      if (debug == true) {
        Serial.print("Pressed: ");
        Serial.println(i, HEX);
      }
      bitWrite(pressedNoteKeys, i, 1);
    } else
      bitWrite(pressedNoteKeys, i, 0);
  }

  // check midi channel
  if (digitalRead(keyModePin) == 0) {
    showMidiCh();
    while (digitalRead(keyModePin) == 0) {

      // midi up?
      if (digitalRead(keyOctUpPin) == 0) {
        if (midiCh < 15)
          midiCh++;
        showMidiCh();
        EEPROM.update(0, midiCh);
        while (digitalRead(keyOctUpPin) == 0)
          delay(20);
      }

      // midi down?
      if (digitalRead(keyOctDownPin) == 0) {
        if (midiCh > 0)
          midiCh--;
        showMidiCh();
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

void playNotes() {
  for (int i = 0; i < maxNoteKeys; i++) {
    if (bitRead(pressedNoteKeys, i) != bitRead(previousNoteKeys, i)) {
      uint8_t note = i + 12 * octave;
      if (bitRead(pressedNoteKeys, i)) {
        bitWrite(previousNoteKeys, i , 1);
        MidiUSB.sendMIDI({0x09, 0x90 | midiCh , note, noteIntensity});
        MidiUSB.flush();
        if (echoLed == true) {
          strip.SetPixelColor(noteLedMap[i], wheelH(map(note, 0, 128, 0, 767)));
          strip.Show();
        }
        if (debug == true) {
          Serial.print("NoteOn Ch:");
          Serial.print(midiCh);
          Serial.print(" Note:");
          Serial.println(note, HEX);
        }
      } else {
        bitWrite(previousNoteKeys, i , 0);
        MidiUSB.sendMIDI({0x08, 0x80 | midiCh , note, 0});
        MidiUSB.flush();
        if (echoLed == true) {
          strip.SetPixelColor(noteLedMap[i], RgbColor(0, 0, 0));
          strip.Show();
        }
        if (debug == true) {
          Serial.print("NoteOff Ch:");
          Serial.print(midiCh);
          Serial.print(" Note:");
          Serial.println(note, HEX);
        }
      }
    }
  }
}

void checkMidi() {
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) {
      if (rx.byte1 == (0x90 | midiCh)) { // tone on
        strip.SetPixelColor(noteLedMap[(rx.byte2 - 0) % 12], wheelH(map(rx.byte2, 0, 128, 0, 767)));     // replace 0 with 4 if using sunvox MIDI echo...
        strip.Show();
      } else if (rx.byte1 == (0x80 | midiCh)) { // tone off
        strip.SetPixelColor(noteLedMap[(rx.byte2 - 0) % 12], RgbColor(0, 0, 0));    // replace 0 with 4 if using sunvox MIDI echo...
        strip.Show();
      }
      if (debug == true) {
        Serial.print("Received: ");
        Serial.print(rx.header, HEX);
        Serial.print("-");
        Serial.print(rx.byte1, HEX);
        Serial.print("-");
        Serial.print(rx.byte2, HEX);
        Serial.print("-");
        Serial.println(rx.byte3, HEX);
      }
    }
  } while (rx.header != 0);
}

void showMidiCh() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= midiCh)
      strip.SetPixelColor(statusLedMap[i], wheelH(map(i, 0, 16, 0, 767)));
    else
      strip.SetPixelColor(statusLedMap[i], RgbColor(0, 0, 0));
    strip.Show();
  }
  fadeLeds = true;
}

void showOctave() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= octave)
      strip.SetPixelColor(statusLedMap[i], wheelH(map(i * 12, 0, 128, 0, 767)));
    else
      strip.SetPixelColor(statusLedMap[i], RgbColor(0, 0, 0));
    strip.Show();
  }
  fadeLeds = true;
}

void fadeStatusLeds() {
  fadeLeds = false;
  for (uint8_t i = 0; i < maxLeds; i++) {
    RgbColor tmpColor = strip.GetPixelColor(i);
    tmpColor.Darken(1);
    if (tmpColor != RgbColor(0, 0, 0))
      fadeLeds = true;
    strip.SetPixelColor(i, tmpColor);
  }
  delay(6);
  strip.Show();
}

void clearLED() {
  for (int i = 0; i < maxLeds; i++) {
    strip.SetPixelColor(i, RgbColor(0, 0, 0));
  }
  strip.Show();
}

void showLedAnimation() {
  for (int j = 0; j <= 180; j++) {        // all off once (18-8)/10=1
    for (int i = 0; i < 8; i++) {
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
