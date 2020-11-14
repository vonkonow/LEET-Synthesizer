//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET Sequencer 0.8 ***=-
//
// 16 step, 8 channel sequncer using 16x8 RGB leds for visualisation.
// Songs are stored on microSD card in custom format (midi-ish).
// The device sends and recieves MIDI events over USB
//
// Uses Arduino pro micro (atMega32U4), WS2812 LED strips, WS2812 LED matrixes,
// SPI microSD card reader, 1 trimpot & 11 keys connected between
// microcontroller and GND (using weak pull-ups).
//
// Key description:
// bpm   clr   dup   swap   dec   inc
// play  pgm   pos   track  patt  song
//
// Latest version, details and build instructions is found on www.vonkonow.com
// This code is open source under MIT License.
// (attribution is optional, but always appreciated - Johan von Konow ;)
//


/*
  Program description:
    Each Song contains one or several Patterns.
    Each Pattern contains 16 positions (beats).
    Each Position contains 24 curTicks and 8 tracks.
    Every position is stored as a separate file containing up to 64 midi events
  with corresponding tick - defining when they are due to be played.
    The files are stored in the following directory structure:
  Song01/Pattern00/Pos12.txt

  Each midievent in PosXX.txt uses 4 bytes, where the first is curTick, followed
  by three standard midi bytes.
  example: 0x00,0x93,0x30,0x7f
  0x00 – send event at first curTick
  0x93 – send “note on” (0x9X) on midiChannel 4 (0xX3)
  0x30 – tone C4
  0x7f – Maximum intensity (127)
  (0xff in first byte means empty position, despite the following 3 bytes.)

  Due to VERY tight memory restrictions, only one position is stored in RAM
  (called midiBuffer). Since loading the position file takes longer than a
  curTick, the midiBuffer is separated in two halves to play and preload
  curTicks independently.
  The lower part of midiBuffer contains 32 bytes and stores events on curTick
  0-11, while the upper buffer stores events on curTick 12-23. At curTick 12,
  the lower buffer of next position is loaded (while playback is initiated from
  the upper buffer).

  Playback is handled by a timer interrupt (using tmr1 @ 1ms) and plays
  corresponding event from the midiBuffer (when seqMode == seqPlay).
  (I prefer to avoid interrupts, but did not want to rewrite SdFat library to
  achieve predictable timing…)

  When switching to a new pattern, there is not time to parse all position files
  to update the LEDdisplay (takes more than 100ms). Instead,
  SongXX/PatternXX/LED.txt contains pre-calculated values for each pixel (16x8).
  This file has to be updated when a position is altered.

  The sequencer operates in one of the following modes
  (selected and indicated by the lower control panel):
  seqStop    – Default mode - all mode LEDs off. Displays current pattern.
  seqPlay    – Plays the content of a position and moves to the next.
  seqPgm     – Stores incoming midiEvents (note on /off) in current position
  seqPos     – Displays notes within the current position.
  seqTrack   – Used to select active track (when editing)
  seqPattern – Displays the patterns of the song. Used to change pattern.
  seqSong    – Displays the different songs on the SD card. Used to change song.

  This program is using the following excellent libraries:
  - SdFat - https://github.com/greiman/SdFat
  - NeoPixelBus - https://github.com/Makuna/NeoPixelBus
  - MIDIUSB - https://github.com/arduino-libraries/MIDIUSB
  The libraries are the smallest I found (tested several others), but still eats
  approximately half of avaliable RAM and Flash on the AtMega32U4...
*/

//
// todo: (if time && memory is avaliable ;)
// * implement midiTick edit 
// * test midi beat clock (24 pulses per beat)
// * highlight command LEDs for avaliable commands
// * test if synch with ISR before led update improves delay
// * add possibility to turn off led display to enable even playback?
// * add longpress or + to delete pattern, song (prevent accidents)
// * add animations after played pattern (pacman instead of rainbow?)
// * remove led[id] to save space.... or replace with keyId array

// History: 2020-11-13
// cleaned up code: renamed variables and improved comments
// changed key order (pos / track)

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
// List of serial error codes (used to preserve memory)
// 0x00 - Info setup() "Serial communication initiated"
// 0x01 - Info setup() "Initializing SD card..."
// 0x02 - Info setup() "...SD initialization done."
// 0x03 - Info getMidi() "incoming midi..."
// 0x04 - Info getMidi() "incoming midi...stored in midiBuffer pos:" (followed by position)
// 0x05 - Info clearChMidiBuffer() "delete all data in midiBuffer from track:" (followed by track)
// 0x06 - Info loadLowerMidiBuffer() "loading lower midiBuffer from file: " (followed by filename)
// 0x07 - Info loadLowerMidiBuffer() "...lower midibuffer loaded"
// 0x08 - Info loadUpperMidiBuffer() "loading upper midiBuffer from file: " (followed by file)
// 0x09 - Info loadUpperMidiBuffer() "...upper midibuffer loaded"
// 0x0A - Info appendMidiBuffer() "Writing file:" (followed by filename)
// 0x0B - Info appendMidiBuffer() "...done."
// 0x0C - Info duplicatePattern() "Renaming pattern directory:" (followed by filename)
// 0x0D - Info duplicatePattern() "...to directory:" (followed by filename)
// 0x0E - Info duplicatePattern() "mkdir:" (followed by filename)
// 0x0F - Info duplicatePattern() "Copying file:" (folowed by pos ID)
// 0x10 - Info duplicatePattern() "...done"
// 0x11 - Info duplicateSong() "Renaming song directory:"(followed by filename)
// 0x12 - Info duplicateSong() "...to directory:" (followed by filename)
// 0x13 - Info duplicateSong() "creating song directory:" (followed by filename)
// 0x14 - Info duplicateSong() "copying position file:" (followed by posId
// 0x15 - Info duplicateSong() "...done"
// 0x16 - Info savePatternCache() "generating pattern cache to led.txt"
// 0x17 - Info restorePatternCache() "loading led.txt for pattern:"
// 0x18 - Info restorePatternCache() "..done"

// 0x80 - Error setup() "SD initialization failed!"
// 0x81 - Error loadLowerMidiBuffer() "could not open file"
// 0x82 - Error loadUpperMidiBuffer() "could not open file"
// 0x83 - Error appendMidiBuffer() "could not open file (write)"
// 0x84 - Error duplicatePattern() "could not rename directory
// 0x85 - Error duplicatePattern() "could not create directory"
// 0x86 - Error duplicateSong() "could not rename song directory"
// 0x87 - Error duplicateSong() "could not create song directory"
// 0x88 - Error duplicateSong() "could not create pattern directory"
// 0x89 - Error swapPos() "could not rename pos file"
// 0x8A - Error swapPattern() "could not rename pattern"
// 0x8B - Error swapSong() "could not rename song"
// 0x8C - Error clearSong() "could not remove pattern cache, led.txt"
// 0x8D - Error clearSong() "could not remove pattern directory"
// 0x8E - Error deleteAllPosFiles() "could not remove poition file"
// 0x8F - Error savePatternCache() "file open (write led.txt)"
// 0x90 - Error restorePatternCache() file open (read led.txt)"
//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|

#include <MIDIUSB.h>
#include <NeoPixelBrightnessBus.h>
#include <SPI.h>                                  // Used by SdFat
#include <SdFat.h>                                // SD card file handling

// bottom row (mode keys):
const uint8_t maxKeys = 11;
const uint8_t keyPlay = 10;
const uint8_t keyPgm = 5;
const uint8_t keyPos = 6;
const uint8_t keyTrack = 7;
const uint8_t keyPattern = 9;
const uint8_t keySong = 8;

// top row (command keys):
const uint8_t keyClear = 4;
const uint8_t keyDuplicate = 3;
const uint8_t keySwap = 18;
const uint8_t keyDec = 20;
const uint8_t keyInc = 19;
const uint8_t keyMap[maxKeys] = {10, 5, 6, 7, 9, 8, 4, 3, 18, 20, 19};
const uint8_t ledMap[12] = {6, 7, 8, 9, 10, 11, 5, 4, 3, 2, 1, 0};

// sequencer run/display modes
const uint8_t seqStop = 0;
const uint8_t seqPlay = 1;
const uint8_t seqPgm = 2;
const uint8_t seqTrack = 3;
const uint8_t seqPos = 4;
const uint8_t seqPattern = 5;
const uint8_t seqSong = 6;

const char fontA[13][3] = {
  {0x0E, 0x11, 0x0E}, // 0
  {0x12, 0x1F, 0x10}, // 1
  {0x1A, 0x15, 0x12}, // 2
  {0x11, 0x15, 0x0E}, // 3
  {0x07, 0x04, 0x1F}, // 4
  {0x17, 0x15, 0x1D}, // 5
  {0x1E, 0x15, 0x1D}, // 6
  {0x19, 0x05, 0x03}, // 7
  {0x1F, 0x15, 0x1F}, // 8
  {0x17, 0x15, 0x1F}, // 9
  {0x1F, 0x15, 0x11}, // E    Song (not used)
  {0x1F, 0x02, 0x1F}, // M    MidiCh (not used)
  {0x1F, 0x04, 0x1F}  // H    Pattern (not used)
};

const bool debug = true;                          // <- serial debugging over USB
const bool startAnimation = true;                 // <- show start animation (LEET)
const uint8_t ledBrightness = 42;                 // <- LED intensity (0-255)
const uint8_t midiBufferLower = 32;               // end of lower buffer (0-31) used for curTick 0-11
const uint8_t midiBufferUpper = 64;               // end of upper buffer (32-63) used for curTick 12-23
const uint8_t posMax = 16;                        // 16 Pos / Pattern
const uint8_t curTickMax = 24;                    // 24 ticks / POS (beat)
const uint8_t ledPin  = 2;                        // the data pin with the LED strip connected.
const uint8_t anPin = A3;                         // the analog pin for BPM potentiometer
const uint8_t anNum = 16;                         // amount of analog filter to reduce noise
const uint8_t maxLeds = 12 + 16 * 8;              // number of LEDs (control + display)

bool midiBufferChanged;                           // flag if changes have been made (requiring position file save)
bool displayUpdate;                               // flag if display needs to be updated
bool displayShow;                                 // flag if display shall be displayed in main loop (curTick==0 || cuTick==12)
bool ledFlash;                                    // flag to indicate 2x BPM
uint8_t curSong;                                  // song id (00-63)
uint8_t curPattern;                               // pattern id (00-15)
uint8_t curPos;                                   // pos id (00-15)
uint8_t curTrack;                                 // selected MIDI channel
uint8_t maxSong;                                  // number of songs
uint8_t maxPattern;                               // number of patterns
uint8_t seqMode;                                  // current mode of sequencer (seqStop, seqPlay etc)
uint8_t anPos = 0;                                // analog filter - the index of the current reading
int anHistory[anNum];                             // analog filter - the last readings from the analog input
int anTotal = 0;                                  // analog filter - the running total
int anAvg = 32;                                   // analog filter - average
char fileName[18] = "/Song00/Pattern00";          // used to store path or fileName
RgbColor rowBackup[8];                            // array to save column colors before showing position

volatile bool playBuffer;                         // flag if midiBuffer shall be played
volatile bool newTick;                            // flag to indicate new tick from interrupt
volatile bool loadLowerBuffer;                    // flag to indicate that next half of midiBuffer is ready for playback
volatile bool loadUpperBuffer;                    // flag to indicate that next half of midiBuffer is ready for playback
volatile uint8_t midiBuffer[midiBufferUpper][4];  // lower and upper midi buffer
volatile uint8_t curTick;                         // current midiTick position
volatile uint8_t msTick;                          // bpm=2500/msTick: 14=180bpm, 42=60bpm (60*1000/24=2500)
volatile uint8_t intC;                            // tmr1 interrupt counter
volatile uint16_t rainbowC;                       // tmr counter used for colorWheel (0-767)

NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(maxLeds, ledPin);
SdFat SD;
File myFile;

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// Main functions that handles setup, ISR & mainLoop
//------------------------------------------------------------------------------

void setup() {
  if (debug == true) {
    Serial.begin(115200);
    //  while (!Serial);                          // wait for serial port to connect. Disable to allow usage without serial port
    Serial.println(0x00);                         // 0x00 - Info "Serial communication initiated"
  }

  for (int i = 0; i < maxKeys; i++)               // init controlpanel buttons (input with weak-pullup)
    pinMode(keyMap[i], INPUT_PULLUP);

  strip.Begin();
  strip.SetBrightness(ledBrightness);
  if (startAnimation == true)
    leet();

  debugInfo(0x01);                                // 0x01 - Info "Initializing SD card..."
  if (!SD.begin(-1)) {                            // -1 => disable SD CS pin
    debugError(0x80);                             // 0x80 - Error "SD initialization failed!"
  }
  debugInfo(0x02);                                // 0x02 - Info "SD initialization done."

  getMaxSong();                                   // get number of songs on SD
  getMaxPattern();                                // get number of patterns in song
  gotoPosDir(curSong, curPattern);                // change directory to /Song00/Pattern00/

  seqMode = seqStop;
  displayUpdate = true;                           // force display update
  loadMidiBuffer(curPos);                         // load upper and lower midiBuffer

  //set timer1 interrupt at 1kHz
  TCCR1A = 0;                                     // set entire TCCR1A register to 0
  TCCR1B = 0;                                     // same for TCCR1B
  TCNT1  = 0;                                     // initialize counter value to 0
  OCR1A = 249;                                    // 1kHz - compare match register = (16*10^6) / (1000*64) - 1 (must be <65536)
  TCCR1B |= (1 << WGM12);                         // turn on CTC mode
  TCCR1B |= (1 << CS11) | (1 << CS10);            // set CS10 and CS11 bits for 64 prescaler
  TIMSK1 |= (1 << OCIE1A);                        // enable timer compare interrupt
}

// ISR is called once every millisecond.
// Increase curTick and send midiEvents in midiBuffer.
// Keep ISR as short as possible since it will interfere with timing critical
// functions like WS2812 LED update (causing random LED flickering).
ISR(TIMER1_COMPA_vect) {
  intC++;
  if (intC >= msTick) {
    intC = 0;
    if (playBuffer == true) {
      //      MidiUSB.sendMIDI({0x0F, 0xF8});             // System Real-Time Messages - Sent 24 times per pos/beat (NOT per quarter note)
      //      MidiUSB.flush();
      for (uint8_t i = 0; i < midiBufferUpper; i++) {
        if (playBuffer == true && midiBuffer[i][0] == curTick && curPos < posMax) {   // takes ~ 50us
          MidiUSB.sendMIDI({midiBuffer[i][1] >> 4, midiBuffer[i][1], midiBuffer[i][2], midiBuffer[i][3]});
          MidiUSB.flush();
        }
      }
      curTick++;
      if (curTick >= curTickMax) {
        curTick = 0;
        loadUpperBuffer = true;
      }
      if (curTick == 12)
        loadLowerBuffer = true;
      newTick = true;
    }
    rainbowC++;                                   // used for animated color gradients
    if (rainbowC >= 767)
      rainbowC = 0;
  }
}

void loop() {                                     // [takes up to 30ms (13+12+5) on tick0)]
  if (newTick == true) {                          // new curTick event occured (in TMR1 ISR):
    newTick = false;                              // reset interrupt flag
    if (loadUpperBuffer == true) {                // Move to next pos and load upperBuffer:
      loadUpperBuffer = false;
      displayShow = true;
      ledFlash = true;                            //  flash bpmLED @ twice the speed of midiPos
      if (seqMode == seqPlay) {
        posInc();                                 //  move top next position in pattern
        loadUpperMidiBuffer(curPos);              //  load upper midi buffer (miditick 12-23 -> midiBuffer 32-63) [takes 4-8ms]
      }
    } else if (loadLowerBuffer == true) {         // Half way thru, toggle BPM led and load lowerBuffer:
      loadLowerBuffer = false;
      displayShow = true;
      ledFlash = false;                           //  flash bpmLED @ twice the speed of midiPos
      if (seqMode == seqPlay) {
        loadNextLowerBuffer(curPos);              //  load next lower midi buffer (miditick 0-11 -> midiBuffer 0-31) [takes 7-13ms]
      }
    }
  }
  getMidi();                                      // handle incoming midi events
  readButtons();                                  // check keys on controlpanel [230us]
  if (curTick == 0 || curTick == 12 || displayShow == true) {
    displayShow = false;
    showStatus();                                 // prepare leds on controlpanel and display (16x8) (no display update) [4.5ms-12ms(with restore)]
    strip.Show();                                 // update display. Disables tmr interrupt and offsets BPM [takes 4.35ms].
  }
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// Functions that handles keys, display and midi on a high level
//------------------------------------------------------------------------------
void readButtons() {
  anUpdate();                                     // check analog input (filtered value)
  msTick = map(anAvg, 0, 255, 50, 2);             // bpm=2500/msTick: 14=180bpm, 42=60bpm
  
  // Mode change?
  testMode(keyPlay, seqPlay);
  testMode(keyPgm, seqPgm);
  testMode(keyTrack, seqTrack);
  testMode(keyPos, seqPos);
  testMode(keyPattern, seqPattern);
  testMode(keySong, seqSong);

  // Check command Increase key
  if (digitalRead(keyInc) == 0) {
    if (seqMode == seqStop || seqMode == seqPlay || seqMode == seqPgm) {
      posInc();
      loadMidiBuffer(curPos);                     // load upper and lower midi buffer
    } else if (seqMode == seqTrack) {
      if (curTrack < 7)
        curTrack++;
    } else if (seqMode == seqPattern) {
      patternInc();
      curPos = 0;
    } else if (seqMode == seqSong)
      songInc();
    while (digitalRead(keyInc) == 0)
      delay(20);
  }

  // Check command Decrease key
  if (digitalRead(keyDec) == 0) {
    if (seqMode == seqStop || seqMode == seqPlay || seqMode == seqPgm) {
      posDec();
      loadMidiBuffer(curPos);                     // load upper and lower midi buffer
    } else if (seqMode == seqTrack) {
      if (curTrack > 0)
        curTrack--;
    } else if (seqMode == seqPattern) {
      patternDec();
      curPos = 0;
    } else if (seqMode == seqSong)
      songDec();
    while (digitalRead(keyDec) == 0)
      delay(20);
  }

  // Check command Clear key
  if (digitalRead(keyClear) == 0) {
    if (seqMode == seqPgm) {
      clearChMidiBuffer(curPos, curTrack);        // Clear position @ curTrack
      savePatternCache();                         // Parse pattern files, create led cache (led.txt) and update display
    }
    if (seqMode == seqTrack) {
      for (uint8_t i = 0; i < 16; i++)            // Clear track (all positions @ curTrack)
        clearChMidiBuffer(i, curTrack);           // Clears a track from midiBuffer and saves pos file.
      savePatternCache();                         // Parse pattern files, create led cache (led.txt) and update display
    }
    if (seqMode == seqPattern) {
      for (uint8_t i = 0; i < 16; i++) {          // Clear Pattern (all positions @ all tracks)
        clearMidiBuffer(0, midiBufferUpper);
        saveMidiBuffer(i);
      }
      savePatternCache();                         // Parse pattern files, create led cache (led.txt) and update display
    }
    if (seqMode == seqSong) {
      clearSong(curSong);                         // Clear song directory and all files and folders within
    }
    displayUpdate = true;
    while (digitalRead(keyClear) == 0)
      delay(20);
  }

  // Check command Duplicate key
  if (digitalRead(keyDuplicate) == 0) {
    if (seqMode == seqPgm) {
      duplicatePos(curPos);                       // duplicate pos @ curTrack (overwrites next pos in same pattern)**** duplicate pos 15 to next pattern
    }
    if (seqMode == seqTrack) {
      duplicateTrack(curTrack);                   // duplicate track (overwrites track in next pattern)
    }
    if (seqMode == seqPattern) {
      duplicatePattern(curPattern);               // move sucessive patterns and duplicate pattern to curPattern + 1 (does not overwrite next pattern)
    }
    if (seqMode == seqSong) {
      duplicateSong(curSong);                     // move sucessive songs and duplicate song to cursong + 1 (does not overwrite next song)
    }
    displayUpdate = true;
    while (digitalRead(keyClear) == 0)
      delay(20);
  }

  // Check command Swap key
  while (digitalRead(keySwap) == 0) {
    if (digitalRead(keyInc) == 0) {               // swap to the right
      if (seqMode == seqPgm && curPos < 15) {
        swapPos(curPos, curPos + 1);              // move pos. moves all tracks & only within a pattern...
        curPos++;
        savePatternCache();                       // Parse pattern files, create led cache (led.txt) and update display
      }
      if (seqMode == seqTrack) {
        // move track **NOT IMPLEMENTED**
      }
      setPatternPath(curSong, curPattern + 1);
      if (seqMode == seqPattern && SD.exists(fileName)) {
        swapPattern(curPattern, curPattern + 1);
        curPattern++;
      }
      if (seqMode == seqSong) {
        swapSong(curSong, curSong + 1);
      }
      displayUpdate = true;
      while (digitalRead(keyInc) == 0)
        delay(20);
    }
    if (digitalRead(keyDec) == 0) {               // swap to the left
      if (seqMode == seqPgm && curPos > 0) {
        swapPos(curPos, curPos - 1);              // move pos. moves all tracks & only within a pattern...
        curPos--;
        savePatternCache();                       // Parse pattern files, create led cache (led.txt) and update display
      }
      if (seqMode == seqTrack) {
        // move track                             // NOT IMPLEMENTED
      }
      if (seqMode == seqPattern && curPattern > 0) {
        swapPattern(curPattern, curPattern - 1);
        curPattern--;
      }
      if (seqMode == seqSong) {
        swapSong(curSong, curSong + 1);
      }
      displayUpdate = true;
      while (digitalRead(keyDec) == 0)
        delay(20);
    }
    delay(20);
  }
  if (seqMode != seqPlay && displayUpdate == true) {      // was playback just stoppped?
    curTick = 0;
    playBuffer = false;                           // stop ISR playback of buffer
  }
}

void testMode(uint8_t key, uint8_t mode) {
  if (digitalRead(key) == 0) {
    if (seqMode != mode) {
      seqMode = mode;
    } else {
      seqMode = seqStop;
    }
    displayUpdate = true;
    while (digitalRead(key) == 0)
      delay(20);
    if (seqMode == seqStop)                       // Did we just stop playback?
      midiStop();                                 // ..yes, send stop
    if (seqMode == seqPlay) {                     // Did we just start playback?
      loadMidiBuffer(curPos);                     // ..yes, load MidiBuffer with curPos
      intC = 0;
      curTick = 0;
      playBuffer = true;                          // All set - initiate playback from ISR
    }
  }
}

void showStatus() {
  if (displayUpdate == true) {                    // generate new display content? (set by new pattern, song, or some key presses)
    displayUpdate = false;                        // clear flag

    // clear and show sequencer screen
    if (seqMode == seqStop || seqMode == seqPlay || seqMode == seqPgm || seqMode == seqTrack) {
      restorePatternCache();                      // Load pattern cache to display
    }

    // update play led and initiate playback if selected
    if (seqMode == seqPlay)
      strip.SetPixelColor(ledMap[0], RgbColor(0, 255, 255));
    else {
      strip.SetPixelColor(ledMap[0], RgbColor(0, 0, 0));
    }

    // update pgm led
    if (seqMode == seqPgm)
      strip.SetPixelColor(ledMap[1], RgbColor(0, 255, 255));
    else
      strip.SetPixelColor(ledMap[1], RgbColor(0, 0, 0));

    // update track led
    if (seqMode == seqTrack)
      strip.SetPixelColor(ledMap[3], wheelH(map(curTrack, 0, 7, 0, 654)));
    else
      strip.SetPixelColor(ledMap[3], RgbColor(0, 0, 0));

    // show pos edit screen (****NOT COMPLETED, only template****)
    if (seqMode == seqPos) {
      int8_t bufPos = 0;
      uint8_t notePitch = 0;
      uint8_t noteStart = 0;
      uint8_t noteEnd = 23;
      uint8_t notePos = 0;
      for (uint8_t i = 0; i < midiBufferUpper; i++) {
        if ((midiBuffer[i][1] & 0x0F) == curTrack)
          bufPos--;
        if (bufPos < 0) {
          notePitch = midiBuffer[i][2];
          notePos =  midiBuffer[i][0];
          if ((midiBuffer[i][1] & 0xF0) == 0x80) {
            noteEnd = midiBuffer[i][0];
            //        noteStart = findBufferNote(notePitch);
          }
          else {
            noteStart = midiBuffer[i][0];
          }
          break;
        }
      }
      clearDisplayLED();
      strip.SetPixelColor(ledMap[2], RgbColor(0, 255, 80));
      for (uint8_t i = noteStart; i <= noteEnd; i++) {
        strip.SetPixelColor(getStripPos(i % 8, i / 8), RgbColor(0, 255, 80));
      }
      font(notePitch / 10, 4, 2, RgbColor(0, 255, 80));
      font(notePitch % 10, 0, 2, RgbColor(0, 255, 80));
      strip.SetPixelColor(getStripPos(notePos % 8, notePos / 8), RgbColor(255, 0, 128));
    } else
      strip.SetPixelColor(ledMap[2], RgbColor(0, 0, 0));

    // show pattern select screen
    if (seqMode == seqPattern) {
      clearDisplayLED();
      strip.SetPixelColor(ledMap[4], RgbColor(0, 155, 255));
      for (uint8_t i = 0; i <= maxPattern; i++) {
        strip.SetPixelColor(getStripPos(i % 8, i / 8), RgbColor(0, 155, 255));
      }
      font(curPattern / 10, 4, 2, RgbColor(0, 155, 255));
      font(curPattern % 10, 0, 2, RgbColor(0, 155, 255));
      strip.SetPixelColor(getStripPos(curPattern % 8, curPattern / 8), RgbColor(255, 0, 128));
    } else
      strip.SetPixelColor(ledMap[4], RgbColor(0, 0, 0));

    // show song select screen
    if (seqMode == seqSong) {
      clearDisplayLED();
      strip.SetPixelColor(ledMap[5], RgbColor(0, 200, 200));
      for (uint8_t i = 0; i <= maxSong; i++) {
        strip.SetPixelColor(getStripPos(i % 8, i / 8), RgbColor(0, 200, 200));
      }
      font(curSong / 10, 4, 2, RgbColor(0, 200, 200));
      font(curSong % 10, 0, 2, RgbColor(0, 200, 200));
      strip.SetPixelColor(getStripPos(curSong % 8, curSong / 8), RgbColor(255, 0, 128));
    } else
      strip.SetPixelColor(ledMap[5], RgbColor(0, 0, 0));
  }

  // update bpm led
  if (ledFlash == true)
    strip.SetPixelColor(5, wheelH(map(msTick, 62, 2, 0, 654)));
  else
    strip.SetPixelColor(5, RgbColor(0, 0, 0));

  if (seqMode == seqStop || seqMode == seqPlay || seqMode == seqPgm || seqMode == seqTrack)
    markLedCol(curPos);                           // Save led pattern & show current position (red column)
}

void getMidi() {
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0 && seqMode == seqPgm) {
      debugInfo(0x03);                            // 0x03 - Info "incoming midi..."
      uint8_t pos;
      if ((rx.byte1 & 0xf0) == 0x90) {            // Note On -> store in lower midiBuffer
        // find empty space in midiBuffer:
        for (pos = 0; pos < midiBufferLower && midiBuffer[pos][0] != 0xff; pos++ ) ;
        if (pos < midiBufferLower) {
          midiBufferChanged = true;
          midiBuffer[pos][0] = 0;                 // at the first Tick of position
          midiBuffer[pos][1] = rx.byte1;
          midiBuffer[pos][2] = rx.byte2;
          midiBuffer[pos][3] = rx.byte3;
        }
      } else if ((rx.byte1 & 0xf0) == 0x80) {     // Note Off -> store in upper midiBuffer
        // find empty space in midiBuffer:
        for (pos = midiBufferLower; pos < midiBufferUpper && midiBuffer[pos][0] != 0xff; pos++ ) ;
        if (pos < midiBufferUpper) {
          midiBufferChanged = true;
          midiBuffer[pos][0] = curTickMax - 1;    // at the last Tick (23) of position
          midiBuffer[pos][1] = rx.byte1;
          midiBuffer[pos][2] = rx.byte2;
          midiBuffer[pos][3] = rx.byte3;
        }
      }
      debugInfo(0x04);                            // 0x04 - Info "incoming midi...stored in midiBuffer pos:"
      Serial.println(pos);
    }
  } while (rx.header != 0);
}

// read and filter BPM potentiometer
void anUpdate() {
  anTotal = anTotal - anHistory[anPos];           // subtract the last reading:
  anHistory[anPos] = analogRead(anPin) / 4;       // read from the sensor: 0-1023 => 0-255
  anTotal = anTotal + anHistory[anPos];           // add the reading to the anTotal:
  anPos++;                                        // advance to the next position in the array:
  if (anPos >= anNum)                             // if we're at the end of the array...
    anPos = 0;                                    // ...wrap around to the beginning:
  anAvg = anTotal / anNum;                        // calculate the average:
}

// send midi command to stop playback and all notes
void midiStop() {
  // First parameter is the event type (0x0B = control change).
  // Second parameter is the event type, combined with the channel.
  // Third parameter is the control number number (0-119).
  // Fourth parameter is the control value (0-127).
  for (uint8_t ch = 0; ch < 16; ch++) {
    MidiUSB.sendMIDI({0x0B, 0xB0 | ch , 0x7B, 0x00}); // Channel Mode Messages - All Notes Off
  }
  MidiUSB.sendMIDI({0x0F, 0xFC});                 // System Real-Time Messages - Stop the current sequence.
  MidiUSB.flush();
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// Functions that handles files, buffers and pointers for navigation
//------------------------------------------------------------------------------

void posInc() {
  restoreLedCol(curPos);
  if (curPos < 15) {
    curPos++;
    saveLedCol(curPos);                           // Save led pattern (overwritten by col)
  } else {                                        // end of pattern
    patternInc();                                 // ..Yes. Goto next pattern and update display (restore led.txt)
  }
}

void posDec() {
  restoreLedCol(curPos);
  if (curPos == 0) {
    curPos = 15;
    patternDec();                                 // goto previous pattern and update display
  } else {
    curPos--;
    saveLedCol(curPos);                           // Save led pattern (overwritten by col)
  }
}

void patternInc() {
  curPattern++;
  if (curPattern > maxPattern) {
    if (seqMode == seqPlay) {                     // end of song, stop playback
      playBuffer = false;
      seqMode = seqStop;
      curTick = 0;
      clearMidiBuffer(0, midiBufferUpper);        // clear midiBuffer
    }
    curPattern = 0;                               // wrap around to first pattern
  }
  gotoPosDir(curSong, curPattern);                // Change directory
  curPos = 0;                                     //
  displayUpdate = true;
}

void patternDec() {
  if (curPattern > 0)
    curPattern--;
  else
    curPattern = maxPattern;
  gotoPosDir(curSong, curPattern);                // Change directory
  curPos = 0;
  displayUpdate = true;
}

void songInc() {
  curSong++;
  if (curSong > maxSong) {
    curSong = 0;                                  // wrap around
  }
  curPos = 0;
  curPattern = 0;
  gotoPosDir(curSong, curPattern);                // change directory to /Song00/Pattern00/
  getMaxPattern();                                // get number of patterns in song (changes directory)
  displayUpdate = true;
}

void songDec() {
  if (curSong > 0)
    curSong--;
  else
    curSong = maxSong;                            // wrap aroun
  curPos = 0;
  curPattern = 0;
  gotoPosDir(curSong, curPattern);                // change directory to /Song00/Pattern00/
  getMaxPattern();                                // get number of patterns in song (changes directory)
  displayUpdate = true;
}

// clear midi events for selected midiChannel
void clearChMidiBuffer(uint8_t pos, uint8_t track) {
  loadMidiBuffer(pos);                            // ensure upper and lower buffer contains current position
  debugInfo(0x05);                                // 0x05 - Info "delete all data in midiBuffer from track:"
  Serial.println(track);
  for (uint8_t i = 0; i < midiBufferUpper; i++)
    if ((midiBuffer[i][1] & 0x0f) == track)
      midiBuffer[i][0] = 0xff;
  saveMidiBuffer(pos);                            // save midiBuffer to file
}

// clear all events in midiBuffer
void clearMidiBuffer(uint8_t startPos, uint8_t stopPos) {
  for (uint8_t i = startPos; i < stopPos; i++)
    midiBuffer[i][0] = 0xff;
}

void loadMidiBuffer(uint8_t fpos) {
  loadLowerMidiBuffer(fpos);
  loadUpperMidiBuffer(fpos);
}

void loadNextLowerBuffer(uint8_t fpos) {          // [7-13ms]
  if (midiBufferChanged == false) {               // dont overwrite altered data
    fpos++;
    if (fpos > 15) {
      fpos = 0;
      if (gotoPosDir(curSong, curPattern + 1))    // end of song?
        loadLowerMidiBuffer(fpos);                // ..no, load buffer from next pattern
      else
        clearMidiBuffer(0, midiBufferLower);      // ..yes, clear part of midi buffer (only curTickPos)
    } else
      loadLowerMidiBuffer(fpos);
    gotoPosDir(curSong, curPattern);              // restore directory
  }
}

void loadLowerMidiBuffer(uint8_t fpos) {          // [4-8ms, depending on filesize]
  if (midiBufferChanged == false) {               // dont overwrite altered data
    clearMidiBuffer(0, midiBufferLower);          // clear part of midi buffer (only curTickPos)
    setPosName(fpos);                             // update fileName
    //    debugInfo(0x06);                        // 0x06 - Info "loading lower midiBuffer from file: "
    //    Serial.print(fileName);
    myFile = SD.open(fileName);
    if (myFile) {
      uint8_t midi = 0, pos = 0;                  // init pointer to midi buffer
      while (myFile.available()) {
        midiBuffer[midi][pos] = myFile.read();
        if (pos < 3)
          pos++;
        else {
          pos = 0;
          if (midiBuffer[midi][0] < 12 && midi < midiBufferLower - 1) // Only curTick 0-11 & Dont write outside buffer.
            midi++;
          else
            midiBuffer[midi][0] = 0xff;
        }
      }
      myFile.close();
    } else
      debugError(0x81);                           // 0x81 - Error "could not open file when loading lower midiBuffer"
    //    debugInfo(0x07);                        // 0x07 - Info "...lower midibuffer loaded"
  }
}

void loadUpperMidiBuffer(uint8_t fpos) {          // [4-8ms, depending on filesize]
  if (midiBufferChanged == false) {               // dont overwrite altered data
    clearMidiBuffer(midiBufferLower, midiBufferUpper);             // clear part of midi buffer (only curTickPos)
    setPosName(fpos);                             // update fileName
    //    debugInfo(0x08);                        // 0x08 - Info "loading upper midiBuffer from file: "
    //    Serial.print(fileName);
    myFile = SD.open(fileName);
    if (myFile) {
      uint8_t midi = midiBufferLower, pos = 0;    // init pointer to midi buffer
      while (myFile.available()) {
        midiBuffer[midi][pos] = myFile.read();    // save next byte from file
        if (pos < 3)
          pos++;
        else {
          pos = 0;
          if (midiBuffer[midi][0] > 11 && midiBuffer[midi][0] < 24 && midi < midiBufferUpper - 1) // Only curTick 12-23 & Dont write outside buffer.
            midi++;
          else
            midiBuffer[midi][0] = 0xff;
        }
      }
      myFile.close();
    } else
      debugError(0x82);                           // 0x82 - Error "could not open file when loading upper midiBuffer"
    //    debugInfo(0x09);                        // 0x09 - Info "...upper midibuffer loaded"
  }
}

void saveMidiBuffer(uint8_t fpos) {
  setPosName(fpos);                               // update fileName
  SD.remove(fileName);                            // delete file -> start with empty file
  appendMidiBuffer(fpos);
}

void appendMidiBuffer(uint8_t fpos) {
  setPosName(fpos);                               // update fileName
  debugInfo(0x0A);                                // 0x0A - Info "Writing file:"
  Serial.print(fileName);
  // open the file. (only one file can be open at a time)
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    for (uint8_t i = 0; i < midiBufferUpper; i++) {
      if (midiBuffer[i][0] != 0xff) {
        myFile.write(midiBuffer[i][0]);
        myFile.write(midiBuffer[i][1]);
        myFile.write(midiBuffer[i][2]);
        myFile.write(midiBuffer[i][3]);
      }
    }
    myFile.close();
    debugInfo(0x0B);                              // 0x0B - Info "...done."
    midiBufferChanged = false;                    // we have saved midiBuffer, clear the flag
  } else {
    debugError(0x83);                             // 0x83 - Error "could not open file (write)"
  }
}

boolean gotoPosDir(uint8_t song, uint8_t pattern) {
  setPatternPath(song, pattern);
  if (!SD.chdir(fileName))
    return (false);
  return (true);
}

// save program memory by only creating this string once...
void setPosName(uint8_t pos) {
  sprintf(fileName, "Pos%02d.txt", pos);
}

// save program memory by only creating this string once...
void setPatternPath(uint8_t song, uint8_t pattern) {
  sprintf(fileName, "/Song%02d/Pattern%02d", song, pattern);
}

// save program memory by only creating this string once...
void setSongPath(uint8_t song) {
  sprintf(fileName, "/Song%02d", song);
}

void getMaxSong() {
  for (uint8_t i = 0; i < 64; i++) {
    setSongPath(i);
    if (SD.exists(fileName))
      maxSong = i;
    else
      i = 64;                                       // directory missing, stop search
  }
}

void getMaxPattern() {
  for (uint8_t i = 0; i < 64; i++) {
    setPatternPath(curSong, i);
    if (SD.exists(fileName))
      maxPattern = i;
    else
      i = 64;                                       // directory missing, stop search
  }
}

void duplicatePos(uint8_t pos) {
  if (pos < 15) {
    // delete track data in destination
    loadMidiBuffer(pos + 1);
    for (uint8_t i = 0; i < midiBufferUpper; i++)
      if ((midiBuffer[i][1] & 0x0f) == curTrack)
        midiBuffer[i][0] = 0xff;
    saveMidiBuffer(pos + 1);
    // copy source track to destination
    loadMidiBuffer(pos);
    for (uint8_t i = 0; i < 64; i++) {
      if ((midiBuffer[i][1] & 0x0f) != curTrack)
        midiBuffer[i][0] = 0xff;
    }
    appendMidiBuffer(pos + 1);
  }
}

void duplicateTrack(uint8_t track) {
  // delete track data in destination
  if (!gotoPosDir(curSong, curPattern + 1)) {
    for (uint8_t j = 0; j < posMax; j++) {
      loadMidiBuffer(j);
      for (uint8_t i = 0; i < midiBufferUpper; i++)
        if ((midiBuffer[i][1] & 0x0f) == track)
          midiBuffer[i][0] = 0xff;
      saveMidiBuffer(j);
    }
    // copy source track to destination
    for (uint8_t j = 0; j < posMax; j++) {
      gotoPosDir(curSong, curPattern);
      loadMidiBuffer(j);
      for (uint8_t i = 0; i < 64; i++) {
        if ((midiBuffer[i][1] & 0x0f) != track)
          midiBuffer[i][0] = 0xff;
      }
      gotoPosDir(curSong, curPattern + 1);
      appendMidiBuffer(j);
    }
  }
}

void duplicatePattern(uint8_t patternId) {
  // rename patterns to create empty space for duplicate
  for (uint8_t i = 63; i > patternId; i--) {
    setPatternPath(curSong, i);
    if (SD.exists(fileName)) {
      debugInfo(0x0C);                            // 0x0C - Info "Renaming pattern directory:"
      Serial.print(fileName);
      if (!SD.rename(fileName, "t"))
        debugError(0x84);                         // 0x84 - Error "could not rename directory"
      setPatternPath(curSong, i + 1);
      debugInfo(0x0D);                            // 0x0D - Info "...to directory:"
      Serial.println(fileName);
      if (!SD.rename("t", fileName))
        debugError(0x84);                         // 0x84 - Error "could not rename directory"
    }
  }
  // create empty directory
  setPatternPath(curSong, patternId + 1);
  debugInfo(0x0E);                                // 0x0E - Info "mkdir:"
  Serial.println(fileName);
  if (!SD.mkdir(fileName))
    debugError(0x85);                             // 0x85 - Error "could not create directory"
  // copy pattern
  for (uint8_t i = 0; i < posMax; i++) {
    debugInfo(0x0F);                              // 0x0F - Info "Copying file:"
    Serial.println(i);
    gotoPosDir(curSong, patternId);
    loadMidiBuffer(i);
    gotoPosDir(curSong, patternId + 1);
    saveMidiBuffer(i);
  }
  savePatternCache();
  getMaxPattern();
  Serial.println(0x10);                           // 0x10 - Info "...done"
}

void duplicateSong(uint8_t song) {
  // rename songs to create empty space for duplicate
  for (uint8_t i = 63; i > song; i--) {
    setSongPath(i);
    if (SD.exists(fileName)) {
      debugInfo(0x11);                            // 0x11 - Info "Renaming song directory:"
      Serial.print(fileName);
      if (!SD.rename(fileName, "t"))
        debugError(0x86);                         // 0x86 - Error "could not rename song directory"
      setSongPath(i + 1);
      debugInfo(0x12);                            // 0x12 - Info "...to directory:" (followed by filename)
      Serial.println(fileName);
      if (!SD.rename("t", fileName))
        debugError(0x86);                         // 0x86 - Error "could not rename song directory"
    }
  }
  // create empty directory
  setSongPath(song + 1);
  debugInfo(0x13);                                // 0x13 - Info "creating song directory:"
  Serial.println(fileName);
  if (!SD.mkdir(fileName))
    debugError(0x87);                             // 0x87 - Error "could not create song directory"
  // copy song with included patterns
  for (uint8_t j = 0; j < 64; j++) {
    setPatternPath(song, j);
    if (SD.exists(fileName)) {
      setPatternPath(song + 1, j);
      if (!SD.mkdir(fileName))
        debugError(0x88);                         // 0x88 - Error "could not create pattern directory"
      for (uint8_t i = 0; i < posMax; i++) {
        debugInfo(0x14);                          // 0x14 - Info "copying position file:"
        Serial.println(i);
        gotoPosDir(song, j);
        loadMidiBuffer(i);
        gotoPosDir(song + 1, j);
        saveMidiBuffer(i);
      }
      savePatternCache();                         // creating a new requires less memory, but longer time...
    }
  }
  getMaxSong();
  debugInfo(0x15);                                // 0x15 - Info "...done"
}

void swapPos(uint8_t p1, uint8_t p2) {
  // move pos (using four rename instead of three to avoid two dynamic strings...)
  setPosName(p1);
  if (!SD.rename(fileName, "1"))
    debugError(0x89);                             // 0x89 - Error "could not rename pos file"
  setPosName(p2);
  if (!SD.rename(fileName, "2"))
    debugError(0x89);                             // 0x89 - Error "could not rename pos file"
  if (!SD.rename("1", fileName))
    debugError(0x89);                             // 0x89 - Error "could not rename pos file"
  setPosName(p1);
  if (!SD.rename("2", fileName))
    debugError(0x89);                             // 0x89 - Error "could not rename pos file"
}

void swapPattern(uint8_t p1, uint8_t p2) {
  // move pattern (using four rename instead of three to avoid two dynamic strings...)
  setPatternPath(curSong, p1);
  if (!SD.rename(fileName, "1"))
    debugError(0x8A);                             // 0x8A - Error "could not rename pattern"
  setPatternPath(curSong, p2);
  if (!SD.rename(fileName, "2"))
    debugError(0x8A);                             // 0x8A - Error "could not rename pattern"
  if (!SD.rename("1", fileName))
    debugError(0x8A);                             // 0x8A - Error "could not rename pattern"
  setPatternPath(curSong, p1);
  if (!SD.rename("2", fileName))
    debugError(0x8A);                             // 0x8A - Error "could not rename pattern"
}

void swapSong(uint8_t s1, uint8_t s2) {
  // move song (using four rename instead of three to avoid two dynamic strings...)
  setSongPath(s1);
  if (!SD.rename(fileName, "1"))
    debugError(0x8B);                             // 0x8B - Error "could not rename song"
  setSongPath(s2);
  if (!SD.rename(fileName, "2"))
    debugError(0x8B);                             // 0x8B - Error "could not rename song"
  if (!SD.rename("1", fileName))
    debugError(0x8B);                             // 0x8B - Error "could not rename song"
  setSongPath(s1);
  if (!SD.rename("2", fileName))
    debugError(0x8B);                             // 0x8B - Error "could not rename song"
  getMaxPattern();                                // get number of patterns in song (changes directory)
}

// using manual method instead of SD.vwd()->rmRfStar(); -> saves 440bytes flash :)
void clearSong(uint8_t song) {
  // Delete all patterns but the first.
  for (uint8_t j = 1; j < 64; j++) {
    if (gotoPosDir(song, j)) {
      deleteAllPosFiles();                        // delete pos*.txt in current directory
      if (!SD.remove("led.txt"))                  // delete file -> start with empty file
        debugError(0x8C);                         // 0x8C - Error "could not remove pattern cache, led.txt"
      setPatternPath(song, j);
      if (!SD.rmdir(fileName)) {
        debugError(0x8D);                         // 0x8D - Error "could not remove pattern directory"
      }
    }
  }
  // create empty pos
  curPattern = 0;
  curPos = 0;
  gotoPosDir(curSong, curPattern);
  deleteAllPosFiles();                            // delete pos*.txt in current directory
  clearMidiBuffer(0, midiBufferUpper);            // clear midiBuffer
  for (uint8_t i = 0; i < posMax; i++)
    saveMidiBuffer(i);                            // write empty file to all positions
  savePatternCache();                             // parse pos files, create led cache (led.txt) and update display
  maxPattern = 1;                                 // get number of patterns in song (changes directory)
}

void deleteAllPosFiles() {
  for (uint8_t i = 0; i < posMax; i++) {
    setPosName(i);                                // update fileName
    if (!SD.remove(fileName))                     // delete file -> start with empty file
      debugError(0x8E);                           // 0x8E - Error "could not remove poition file"
  }
}

void renameError() {
  Serial.println(F("! renaming directory"));      // Save memory by creating string once.
}

// Generate display image from all pattern pos files.
// this takes > 100ms so needs to be cached in led.txt to allow smooth playback
void savePatternCache() {
  debugInfo(0x16);                                // 0x16 - Info "generating pattern cache to led.txt"
  SD.remove("led.txt");                           // delete file -> start with empty file
  for (uint8_t pos = 0; pos < posMax; pos++) {
    loadMidiBuffer(pos);                          // load upper and lower midi buffer
    myFile = SD.open("led.txt", FILE_WRITE);
    if (myFile) {
      for (uint8_t i = 0; i < midiBufferUpper; i++) {
        if (midiBuffer[i][0] != 0xff) {           // creates duplicate for note on, note off and chords, but its ok ;)
          myFile.write(midiBuffer[i][2]);
          myFile.write(getStripPos(pos, midiBuffer[i][1] & 0x0f));
        }
      }
      myFile.close();                             // close led.txt
    } else {
      debugError(0x8F);                           // 0x8F - Error "file open (write led.txt)"
    }
  }
  restorePatternCache();                          // Show pattern cache
}

// takes ~11ms to complete, do it on last tick (pos16,MidiTick23) without updating display?
// can also be done on pos0, tick1 after buffer is read => slight lag on disp update...
void restorePatternCache() {
  //  debugInfo(0x17);                            // 0x17 - Info "loading led.txt for pattern:"
  //  Serial.print(curPattern);
  clearDisplayLED();
  myFile = SD.open("led.txt");
  if (myFile) {
    while (myFile.available()) {
      strip.SetPixelColor(myFile.read(), wheelH(map(myFile.read(), 0, 127, 0, 767)));
    }
    myFile.close();
  } else
    debugError(0x90);                             // 0x90 - Error file open (read led.txt)"
  saveLedCol(curPos);                             // Save led pattern (overwritten by col)
  //  debugInfo(0x18);                            // 0x18 - Info "..done"
}

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// Functions that handles LED display
//------------------------------------------------------------------------------

void  font(uint8_t value, uint8_t xo, uint8_t yo, RgbColor color) {
  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 5; y++) {
      if (bitRead(fontA[value][2 - x], y)) {
        strip.SetPixelColor((8 - y - yo) * 8 + (x + xo) + 12, color);
      }
    }
  }
}

//used by posInc() & posDec()
void restoreLedCol(uint8_t col) {
  if (midiBufferChanged == true) {
    saveMidiBuffer(curPos);                       // save midiBuffer to file
    savePatternCache();                           // Parse pattern files, create led cache (led.txt) and update display.
  } else
    for (uint8_t i = 0; i < 8; i++) {
      if (seqMode == seqPlay)
        strip.SetPixelColor(getStripPos(col, i), wheelH(rainbowC + i * 16));  // rainbow vomit...
      else
        strip.SetPixelColor(getStripPos(col, i), rowBackup[i]);           // restore backuped LEDs
      //  strip.SetPixelColor(getStripPos(col, i), wheelH(500 + i * 32));
    }
}

void saveLedCol(uint8_t col) {
  for (uint8_t i = 0; i < 8; i++) {
    rowBackup[i] = strip.GetPixelColor(getStripPos(col, i));
  }
}

void markLedCol(uint8_t col) {
  for (uint8_t i = 0; i < 8; i++) {
    if (curTrack == i && ledFlash == true)
      strip.SetPixelColor(getStripPos(col, i), RgbColor(255, 0, 255));  // mark current track
    else
      strip.SetPixelColor(getStripPos(col, i), RgbColor(255, 0, 128));  // mark current row
  }
  //  strip.SetPixelColor(getStripPos(col, i), wheelH(rainbowC + i * 16));
  //  strip.SetPixelColor(getStripPos(col, i), wheelH(500 + i * 32));
}

void ledTest() {
  for (int j = 0; j <= 1500; j++) {               // all off once (17-7)/10=1
    for (int i = 0; i < 140; i++) {
      float intensity = (float(j / 10) - i) / 10;
      if (intensity < 0) intensity = 1;           // wait until your turn
      if (intensity > 1) intensity = 1;           // stay off
      strip.SetPixelColor(i, RgbColor::LinearBlend(wheelH(i * 90), RgbColor(0, 0, 0), intensity));
    }
    strip.Show();
  }
}

void leet() {
  for (int offset = 0; offset < 300; offset++) {
    ledRGBox(4, 1, 4, 5, offset);                 // I
    ledRGBox(5, 5, 11, 5, offset);                // _
    ledRGBox(7, 3, 10, 3, offset);                // -
    ledRGBox(8, 2, 9, 4, offset);                 // I
    ledRGBox(6, 1, 14, 1, offset);                // ^
    ledRGBox(13, 2, 13, 5, offset);               // I
    strip.Show();
  }
}

// optimized ledBox with animated rainbow (offset) and where x0<=x1 && y0<=y1
void ledRGBox(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, int offset) {
  for (uint8_t y0 = y1; y0 <= y2; y0++) {
    for (uint8_t x0 = x1; x0 <= x2; x0++) {
      strip.SetPixelColor(getStripPos(x0, y0), wheelH(x0 * 70 + y0 * 30 - offset));
    }
  }
}

uint8_t getStripPos(uint8_t xPos, uint8_t yPos) {
  if (xPos < 8)
    return (139 - xPos  - yPos * 8);              // (7 - xPos)  + (7 - yPos) * 8 + 12 + 64)
  else
    return (83 - xPos - yPos * 8);                // (7 - xPos + 8) + (7 - yPos) * 8 + 12
}

void clearAllLED() {
  clearStatusLED();
  clearDisplayLED();
}

void clearStatusLED() {
  for (uint8_t i = 0; i < 12; i++)
    strip.SetPixelColor(i, RgbColor(0, 0, 0));
}

void clearDisplayLED() {
  for (uint8_t i = 12; i < maxLeds; i++)
    strip.SetPixelColor(i, RgbColor(0, 0, 0));
}

// Input a value 0 to 767 to get corresponding rainbow color
RgbColor wheelH(int WheelPos) {
  while ( WheelPos < 0)
    WheelPos += 768;
  while ( WheelPos >= 768)
    WheelPos -= 768;
  if (WheelPos >= 512)
    return RgbColor(WheelPos - 512, 0, 767 - WheelPos);
  if (WheelPos >= 256)
    return RgbColor(0, 511 - WheelPos, WheelPos - 256);
  return RgbColor(255 - WheelPos, WheelPos, 0);
}

void debugInfo(uint8_t id) {
  if (debug == true) {
    Serial.print("info:");
    Serial.println(id, HEX);
  }
}

void debugError(uint8_t id) {
  if (debug == true) {
    Serial.print("*ERROR*:");
    Serial.println(id, HEX);
  }
}


//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|

/*
  //----------------------------------------------------------------------------
  // Various functions useful during development
  //----------------------------------------------------------------------------

  void debugMidiBuffer() {
  Serial.print(F("Tick: "));
  Serial.println(curTick);
  Serial.print(F("Pos: "));
  Serial.println(curPos);
  Serial.print(F("Track: "));
  Serial.println(curTrack);
  Serial.print(F("Pattern: "));
  Serial.println(curPattern);
  Serial.print(F("Song: "));
  Serial.println(curSong);
  Serial.println(F("midiBuffer:"));
  for (uint8_t i = 0; i < midiBufferUpper; i++) {
    Serial.print(i);
    Serial.print(':');
    Serial.print(midiBuffer[i][0], HEX);
    Serial.print('_');
    Serial.print(midiBuffer[i][1], HEX);
    Serial.print('_');
    Serial.print(midiBuffer[i][2], HEX);
    Serial.print('_');
    Serial.println(midiBuffer[i][3], HEX);
  }
  }

  void debugPos() {
  Serial.print(F("---------------- "));
  Serial.println(curPos, HEX);
  for (uint8_t i = 0; i < curPos; i++)
    Serial.print(" ");
  Serial.println("^");
  }

  void debugTick() {
  Serial.print(F("-----------|------------ "));
  Serial.println(curTick, HEX);
  for (uint8_t i = 0; i < curTick; i++)
    Serial.print(F(" "));
  Serial.println("^");
  }

  // Function that clean (rebuilds) all files and cache on SD-card.
  void cleanFiles() {
  for (uint8_t k = 0; k < 63; k++) {
    setSongPath(k);
    if (SD.exists(fileName)) {
      Serial.print(F("Cleaning song"));
      Serial.println(fileName);
      for (uint8_t j = 0; j < 64; j++) {
        if (gotoPosDir(k, j)) {
          Serial.print(F("Cleaning pattern"));
          Serial.println(fileName);
          for (uint8_t i = 0; i < posMax; i++) {
            Serial.print(F(","));
            Serial.print(i);
            loadMidiBuffer(i);
            saveMidiBuffer(i);
          }
          Serial.println(F("...new chache..."));
          savePatternCache();
        }
      }
    }
  }
  Serial.println(F("...Clean done"));
  }
*/

//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
