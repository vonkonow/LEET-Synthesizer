//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET chord keyboard module ***=-
//
// The device sends MIDI commands over USB when chord keys are pressed.
// Number of notes in the chord, root, scale and tonic can be configured.
// Uses Arduino pro micro (atMega32U4), WS2812 led strips, potentiometer &
// 14 keys connected between microcontroller and GND (using weak pull-ups).
// Latest version, details and build instructions is found on www.vonkonow.com
//
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//
// Future improvements:
// *use flashing led to show selected tonic (replace darken).
// *prevent notes outside range
// *transpose color to 88 key piano instead of 128 key midi?
//
// History: 2020-11-02
// * added startup LED animation
// * midichannel is stored in (non volatile) eeprom memory
// * improved octave and midi-ch visualzation
// * cleaned up code

#include <MIDIUSB.h>
#include <NeoPixelBrightnessBus.h>
#include <EEPROM.h>
// #include <PrintStream.h>                       // used for serial debugging

const bool debug = false;                         // <- serial debugging over USB
const bool echoLed = true;                        // <- echo LED without incoming MIDI
const bool startAnimation = true;                 // <- show start animation
const uint8_t noteIntensity = 64;                 // <- MIDI note volume (0-128)
const uint8_t ledBrightness = 42;                 // <- LED intensity (0-255)
const uint8_t anNum = 8;                          // amount of analog filtering
const uint8_t ledpin  = 2;                        // The data pin with the LED strip connected.
const uint8_t anPin = A3;
const uint8_t maxLeds = 12;
const uint8_t maxKeys = 14;
const uint8_t keyModePin = 19;
const uint8_t keyOctUpPin = 18;
const uint8_t keyOctDownPin = 10;
const uint8_t keyNaturalsPin = 6;
const uint8_t keySharpsPin = 5;
const uint8_t keyMajorPin = 3;
const uint8_t keyMinorPin = 4;
const uint8_t keyMap[maxKeys] = {7, 8, 9, 20, 15, 16, 14, 6, 5, 3, 4, 10, 18, 19};
const uint8_t ledMap[maxLeds] = {5, 6, 7, 8, 9, 10, 11, 4, 3, 2, 1, 0};
const uint8_t minor[9] = {2, 1, 2, 2, 1, 2, 2, 2, 1}; // major = i+2
const uint8_t natural[7] = {0, 2, 4, 5, 7, 9, 11};
const uint8_t accidental[7] = {0, 1, 3, 0, 6, 8, 10};
const uint8_t natOrAcc[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

// Runnig average analog filter:
int anHistory[anNum];                             // last reading from the analog input
uint8_t anPos = 0;                                // index of the current reading
int anTotal = 0;                                  // running total
int anAvg = 32;                                   // analog average

uint8_t scaleNotes[7] = {0, 2, 4, 5, 7, 9, 11};   // start with c major
uint8_t curOctave = 4;                            // selected octave
uint8_t curTonic = 0;                             // 0 = C + curOctave
uint8_t curScale = 2;                             // 0 = minor, 2 = major
uint8_t chordLength = 3;                          // notes in chord
uint8_t midiCh;                                   // start MIDI channel
uint16_t waitChordDim;                            // timer before diming chord LED indication
uint16_t pressedChordKeys = 0x0000;
uint16_t previousChordKeys = 0x0000;
bool fadeLeds = false;

NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(maxLeds, ledpin);

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
  checkKeys();                                    // chords to send are stored in pressedChordKeys
  showStatus();                                   // update status LEDs
  playNotes();                                    // send MIDI notes (if user input)
  checkMidi();                                    // check incoming MIDI and update corresponding LEDs
  delay(1);                                       // slight delay to control dim speed
}

//----------------------------------------------------------------------------------------

void checkKeys() {
  checkChordKeys();
  checkTonicKeys();
  checkChordLength();
  checkScaleKeys();
  checkMidiKeys();
  checkOctaveKeys();
}

void showStatus() {
  if (waitChordDim > 0) {
    waitChordDim--;                               // delay after changing chord length
  } else {
    if (fadeLeds == true) {                       // fade status leds? (change chord length, octave or midi)
      fadeLeds = false;
      for (uint8_t i = 0; i < maxLeds; i++) {
        RgbColor tmpColor = strip.GetPixelColor(i);
        tmpColor.Darken(1);
        if (tmpColor != RgbColor(0, 0, 0))
          fadeLeds = true;
        strip.SetPixelColor(i, tmpColor);
      }
      delay(4);
      strip.Show();
    } else {                                      // display status configuration
      if (curScale == 2) {  // major
        strip.SetPixelColor(0, RgbColor(0, 0, 0));
        strip.SetPixelColor(1, RgbColor(0, 255, 0));
      }
      if (curScale == 0) {  // minor
        strip.SetPixelColor(0, RgbColor(0, 0, 255));
        strip.SetPixelColor(1, RgbColor(0, 0, 0));
      }
      strip.SetPixelColor(2, wheelH(map(chordLength - 1, 0, 7, 0, 767)));
      if (natOrAcc[curTonic]) {
        strip.SetPixelColor(3, wheelH(map(curTonic + curOctave * 12, 0, 128, 0, 767)));
        strip.SetPixelColor(4, RgbColor(0, 0, 0));
      } else {
        strip.SetPixelColor(3, RgbColor(0, 0, 0));
        strip.SetPixelColor(4, wheelH(map(curTonic + curOctave * 12, 0, 128, 0, 767)));
      }
    }
  }
  strip.Show();
}

void playNotes() {
  for (uint8_t i = 0; i < 7; i++) {
    if (bitRead(pressedChordKeys, i) != bitRead(previousChordKeys, i)) {
      if (bitRead(pressedChordKeys, i)) {
        bitWrite(previousChordKeys, i , 1);
        for (uint8_t j = 0; j < chordLength; j++) {                                                         // generate notes in chord...
          uint8_t tmpNote = scaleNotes[(i + j * 2) % 7] % 12;                                               // ensure note is within one octave
          uint8_t tmpOctave = curOctave + (i + j * 2) / 7 + scaleNotes[(i + j * 2) % 7] / 12;               // calculate octave
          //          Serial << F("NoteOn:") << tmpNote << F(" octave:") << tmpOctave  << F(" ch:") << midiCh << endl;
          MidiUSB.sendMIDI({0x09, 0x90 | midiCh, tmpNote + tmpOctave * 12, noteIntensity});
          MidiUSB.flush();
          if (echoLed == true) {
            strip.SetPixelColor(ledMap[(i + j * 2) % 7], wheelH(map(tmpNote + tmpOctave * 12, 0, 128, 0, 767)));
            strip.Show();
          }
        }
      } else {
        bitWrite(previousChordKeys, i , 0);
        for (uint8_t j = 0; j < chordLength; j++) {                                                         // generate notes in chord...
          uint8_t tmpNote = scaleNotes[(i + j * 2) % 7] % 12;                                               // ensure note is within one octave
          uint8_t tmpOctave = curOctave + (i + j * 2) / 7 + scaleNotes[(i + j * 2) % 7] / 12;               // calculate octave
          //          Serial << F("NoteOff:") << tmpNote << F(" octave:") << tmpOctave  << F(" ch:") << midiCh << endl;
          MidiUSB.sendMIDI({0x08, 0x80 | midiCh, tmpNote + tmpOctave * 12, 0});
          MidiUSB.flush();
          if (echoLed == true) {
            strip.SetPixelColor(ledMap[(i + j * 2) % 7], RgbColor(0, 0, 0));
            strip.Show();
          }
        }
      }
    }
  }
}

void checkChordKeys() {
  for (uint8_t i = 0; i < 7; i++) {
    if (digitalRead(keyMap[i]) == LOW) {
      bitWrite(pressedChordKeys, i, 1);
    } else
      bitWrite(pressedChordKeys, i, 0);
  }
}

void checkTonicKeys() {
  // check tonic note (natural)
  if (digitalRead(keyNaturalsPin) == 0) {
    showNatural();
    while (digitalRead(keyNaturalsPin) == 0) {
      for (uint8_t i = 0; i < 7; i++) {
        if (digitalRead(keyMap[i]) == LOW) {
          curTonic = natural[i];
          showNatural();
        }
      }
      delay(20);    // debounce key
    }
    //    Serial << F("Tonic note: ") << hex << curTonic << endl;
    generateScale();
  }
  // check tonic note (accidental)
  if (digitalRead(keySharpsPin) == 0) {
    showAccidental();
    while (digitalRead(keySharpsPin) == 0) {
      for (uint8_t i = 0; i < 7; i++) {
        if (digitalRead(keyMap[i]) == LOW && accidental[i] > 0) {
          curTonic = accidental[i];
          showAccidental();
        }
      }
      delay(20);    // debounce key
    }
    //    Serial << F("Tonic note: ") << hex << curTonic << endl;
    generateScale();
  }
}

void generateScale() {
  uint8_t tmpNote = curTonic;
  //  Serial << F("ScaleNotes:");
  for (uint8_t i = 0; i < 7; i++) {
    scaleNotes[i] = tmpNote;
    //    Serial << dec << tmpNote << ',';
    tmpNote += minor[(i + curScale) % 12];
  }
  //  Serial << endl;
}

void checkChordLength() {
  anUpdate();                                     // check analog chord length (filtered value)
  if (chordLength != map(anAvg, 0, 256, 1, 8)) {
    chordLength = map(anAvg, 0, 256, 1, 8);
    showChordLength();
    //    Serial << F("Chord length:") << chordLength << F(" AN:") << anAvg << endl;
    waitChordDim = 512;
  }
}

void anUpdate() {
  anTotal = anTotal - anHistory[anPos];           // subtract the last reading:
  anHistory[anPos] = analogRead(anPin) / 4;       // read from the sensor: 0-1023 => 0-255
  anTotal = anTotal + anHistory[anPos];           // add the reading to the anTotal:
  anPos++;                                        // advance to the next position in the array:
  if (anPos >= anNum)                             // if we're at the end of the array...
    anPos = 0;                                    // ...wrap around to the beginning:
  anAvg = anTotal / anNum;                        // calculate the average:
  //Serial << F("Analog raw:") << analogRead(anPin) << F(" AnAvg:") << anAvg << endl;
}

void checkScaleKeys() {
  // check major
  if (digitalRead(keyMajorPin) == 0) {
    curScale = 2;
    while (digitalRead(keyMajorPin) == 0)
      delay(20);
    //    Serial << F("Major scale selected: ") << endl;
    generateScale();
  }
  // check minor
  if (digitalRead(keyMinorPin) == 0) {
    curScale = 0;
    while (digitalRead(keyMinorPin) == 0)
      delay(20);
    //    Serial << F("Minor scale selected: ") << endl;
    generateScale();
  }
}

void checkMidiKeys() {
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
    //    Serial << F("Midi ch: ") << hex << midiCh << endl;
  }
}

void checkOctaveKeys() {
  // check Octave Up
  if (digitalRead(keyOctUpPin) == 0) {
    if (curOctave < 8)
      curOctave++;
    showOctave();
    while (digitalRead(keyOctUpPin) == 0)
      delay(20);
    //    Serial << F("Octave: ") << hex << curOctave << endl;
  }

  // checkOctaveKeys Down
  if (digitalRead(keyOctDownPin) == 0) {
    if (curOctave > 0)
      curOctave--;
    showOctave();
    while (digitalRead(keyOctDownPin) == 0)
      delay(20);
    //    Serial << F("Octave: ") << hex << curOctave << endl;
  }
}

// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel can be anything between 0-15. Typically reported to the user as 1-16.
// Third parameter is the note number (48 = middle C).
// Fourth parameter is the velocity (64 = normal, 127 = fastest).
void checkMidi() {
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) {
      //      Serial << F("Channel message: ") << hex << rx.header << ' ' << rx.byte1 << ' ' << rx.byte2 << ' ' << rx.byte3 << endl;
      if (rx.byte1 == (0x90 | midiCh)) { // tone on
        for (uint8_t i = 0; i < 7; i++) {
          if (scaleNotes[i] == (rx.byte2 - 0) % 12) {       // replace 0 with 4 if using sunvox MIDI echo...
            strip.SetPixelColor(ledMap[i], wheelH(map(rx.byte2, 0, 128, 0, 767)));
            strip.Show();
          }
        }
      } else if (rx.byte1 == (0x80 | midiCh)) { // tone off
        for (uint8_t i = 0; i < 7; i++) {
          if (scaleNotes[i] == (rx.byte2 - 0) % 12) {       // replace 0 with 4 if using sunvox MIDI echo...
            strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
            strip.Show();
          }
        }
      }
    }
  } while (rx.header != 0);
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|

void showNatural() {
  for (uint8_t i = 0; i < 7; i++) {
    strip.SetPixelColor(ledMap[i], wheelH(map(natural[i] + curOctave * 12, 0, 128, 0, 767)));
    if (natural[i] != curTonic)
      darkenLED(ledMap[i], 100);
  }
  strip.Show();
  fadeLeds = true;
}

void showAccidental() {
  for (uint8_t i = 0; i < 7; i++)
    strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
  for (uint8_t i = 0; i < 7; i++) {
    if (accidental[i] > 0)
      strip.SetPixelColor(ledMap[i], wheelH(map(accidental[i] + curOctave * 12, 0, 128, 0, 767)));
    if (accidental[i] != curTonic)
      darkenLED(ledMap[i], 100);
  }
  strip.Show();
  fadeLeds = true;
}

void showMidi() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= midiCh)
      updateLED(ledMap[i], wheelH(map(i, 0, 16, 0, 767)));
    else
      updateLED(ledMap[i], RgbColor(0, 0, 0));
  }
  fadeLeds = true;
}

void showOctave() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i <= curOctave)
      updateLED(ledMap[i], wheelH(map(i * 12, 0, 128, 0, 767)));
    else
      updateLED(ledMap[i], RgbColor(0, 0, 0));
  }
  fadeLeds = true;
}

void showChordLength() {
  for (uint8_t i = 0; i < maxLeds; i++) {
    if (i < chordLength)
      updateLED(ledMap[i], wheelH(map(i, 0, 7, 0, 767)));
    else
      updateLED(ledMap[i], RgbColor(0, 0, 0));
  }
  strip.Show();
  fadeLeds = true;
}

void showChord(uint8_t cNote, uint8_t tones, uint8_t scale) { //root note, number of tones in chord, scale type
  uint8_t pos;
  for (uint8_t i = 0; i < tones; i++) {
    cNote += minor[i + scale];       // 0 = minor scale, 2 = major scale
    pos = cNote;
    if (pos > 11)
      pos -= 12;
    strip.SetPixelColor(ledMap[pos], wheelH(map(cNote + curOctave * 12, 0, 128, 0, 767)));
  }
  strip.Show();
}

void updateLED(int pos, RgbColor color) {
  strip.SetPixelColor(pos, color);
  strip.Show();
}

void darkenLED(uint8_t pos, uint8_t dark) {
  RgbColor tmpColor = strip.GetPixelColor(pos);
  tmpColor.Darken(dark);
  strip.SetPixelColor(pos, tmpColor);
}

void clearLED() {
  for (int i = 0; i < maxLeds; i++)
    strip.SetPixelColor(ledMap[i], RgbColor(0, 0, 0));
  strip.Show();
}

void showLedAnimation() {
  for (uint8_t j = 0; j <= 220; j++) {            // (12+10)*10
    for (uint8_t i = 0; i < 12; i++) {
      float intensity = (float(j / 10) - i) / 10;
      if (intensity < 0) intensity = 1;           // wait until your turn
      if (intensity > 1) intensity = 1;           // stay off
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
