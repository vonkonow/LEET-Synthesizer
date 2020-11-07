//-----10|-------20|-------30|-------40|-------50|-------60|-------70|-------80|
//
// -=*** LEET Arpeggiator POC ***=-
//
// The device sends configurable arpeggios as MIDI commands over USB.
// Uses Arduino pro micro (atMega32U4), WS2812 led strips & 7 keys
// connected between microcontroller and GND (using weak pull-ups).
// Latest version, details and build instructions is found on www.vonkonow.com
//
// This project is an experiment to see how an arpeggiator could be
// implemented using the control hardware. It is a proof of concept where I
// have merged Dmirys arpeggiator1 project with the control surface library
// and adjusted it to the control hardware. There is no selection of octaves
// or midi channel and the four note keys are not in order… 
// The implementation is using incoming MIDI signals to select chord note
// (ch3) and arpeggio note (ch2). By using two LEET keyboards and reroute the
// MIDI signals thru the DAW to the arpeggiator they can be used for chord and
// arpeggiator control.
// The potentiometers are used to adjust:
// •    Delay between notes (AN9 0-500ms)
// •    Music Mode (AN8 Ionian, dorian, phrygian, lydian, mixolydian, aeolian,
//      harmonic, locrian)
// •    Arpeggio style (AN7 up, down, up+down, rnd)
// •    Arpeggio Steps (AN6 1-6)
// 
// Future development:
// I plan to focus my attention on rebuilding it from scratch rather than
// improving the current state:
// It will likely be based on the chord unit, allowing it to be played without
// incoming midi channels and it will probably be using permissive open source
// instead of the copy left GPL3.0 license used due to the current code base.
// 

#include <TimerOne.h>
#include <FastLED.h>          // Must be before Control Surface to enable FastLED features of Control Surface
#include <Control_Surface.h>  // Include the Control Surface library

Array<CRGB, 4> leds = {};     // Define the array of leds.
constexpr uint8_t ledpin = 2; // The data pin with the strip connected.
const uint8_t ledId[4] = {3, 2, 1, 0};

typedef enum {C, CD, D, DD, E, F, FD, G, GD, A, AD, B} notes;
typedef enum {maj, minor, dim, aug} chord_types;

typedef struct {
  short Shift;
  chord_types chord_type;
} chord;

class arp {
  private:
    notes baseNote;
    short baseOctave;
    short octaveShift;
    short steps;
    //unsigned int mode[6];
    short indelay;
    int progression;
    chord *mode;
    int order;
  public:
    arp();
    arp(notes bn, short bo, short os, unsigned short st, unsigned int d, unsigned m, unsigned int p);
    void setupArp(short bn, short bo, short os, unsigned short st, unsigned int d, int m, unsigned imode);
    int setProgression(unsigned int p);
    void play();
    void midibegin();
};

int* createChord(notes root, chord_types i, int *notes_array, unsigned short *sh1, unsigned short *sh2);
short midiByNote (notes note, short octave);

#define c_ionian      0
#define c_dorian      1
#define c_phrygian    2
#define c_lydian      3
#define c_mixolydian  4
#define c_aeolian     5
#define c_harmonic    6
#define c_locrian     7
// C Ionian     { C maj;  D min;  E min;  F maj;  G maj;  A min;  B dim  }
const chord ionian[7]     = {{0, maj},    {2, minor}, {4, minor}, {5, maj},   {7, maj},   {9, minor}, {11, dim}};
// C Dorian     { C min;  D min;  D♯ maj; F maj;  G min;  A dim;  A♯ maj  }
const chord dorian[7]     = {{0, minor},  {2, minor}, {3, maj},   {5, maj},   {7, minor}, {9, dim},   {10, maj}};
//C Phrygian    { C min;  C♯ maj; D♯ maj; F min;  G dim;  G♯ maj; A♯ min  }
const chord phrygian[7]   = {{0, minor},  {1, maj},   {3, maj},   {5, minor}, {7, dim},   {8, maj},   {10, minor}};
//C Lydian      { C maj;  D maj;  E min;  F♯ dim; G maj;  A min;  B min   }
const chord lydian[7]     = {{0, maj},    {2, maj},   {4, minor}, {6, dim},   {7, maj},   {9, minor}, {11, minor}};
//C Mixolydian  { C maj;  D min;  E dim;  F maj;  G min;  A min;  A♯ maj  }
const chord mixolydian[7] = {{0, maj},    {2, minor}, {4, dim},   {5, maj},   {7, minor}, {9, minor}, {10, maj}};
//C Aeolian     { C min;  D dim;  D♯ maj; F min;  G min;  G♯ maj; A♯ maj  }
const chord aeolian[7]    = {{0, minor},  {2, dim},   {3, maj},   {5, minor}, {7, minor}, {8, maj},   {10, maj}};
//C harmonic    { C min;  D dim;  D♯ aug; F min;  G maj;  G♯ maj; B dim   }
const chord harmonic[7]   = {{0, minor},  {2, dim},   {3, aug},   {5, minor}, {7, maj},   {8, maj},   {11, dim}};
//C Locrian     { C dim;  C♯ maj; D♯ min; F min;  F♯ maj; G♯ maj; A♯ min  }
const chord locrian[7]    = {{0, dim},    {1, maj},   {3, minor}, {5, minor}, {6, maj},   {8, maj},   {10, minor}};
const chord *all_chords[8] = {ionian, dorian, phrygian, lydian, mixolydian, aeolian, harmonic, locrian};

arp a(C, 5, 2, 6, 200, c_harmonic, 0);
bool button_pressed;
int ButtonVal;

//#define baseNotepin 1
//#define baseOctavepin 5
//#define octaveShiftpin 0
#define indelaypin A9
#define modepin A8
#define orderpin A7
#define stepspin A6
//#define LEDPin 13

// Synchronization: choose one of two possible options:
//#define EXT_SYNC
//#define syncinpin 3
#define INT_SYNC

uint8_t prevstate = 1;
uint8_t midi3note = 0;
uint8_t midi3octave = 4;
uint8_t midi4note = 0;
uint8_t midi4octave = 3;

USBMIDI_Interface midi;     // The MIDI over USB interface to use

// Custom MIDI callback that prints incoming messages.
struct MyMIDI_Callbacks : MIDI_Callbacks {

  // Callback for channel messages (notes, control change, pitch bend, etc.).
  void onChannelMessage(Parsing_MIDI_Interface &midi) override {
    ChannelMessage cm = midi.getChannelMessage();
    // Print the message
    Serial << F("Channel message: ") << hex << cm.header << ' ' << cm.data1
           << ' ' << cm.data2 << dec << F(" on cable ") << cm.CN << endl;
    if (cm.header == (0x90 | 0x03) ) {
      midi4note = cm.data1 % 12;
      midi4octave = cm.data1 / 12;
      button_pressed = true;

      Serial.print("base note:");
      Serial.print(midi4note, HEX);
      Serial.print(" base octave");
      Serial.println(midi4octave, HEX);
    }   
    if (cm.header == (0x90 | 0x02) ) {
      midi3note = cm.data1 % 12;
      midi3octave = cm.data1 / 12;
      button_pressed = true;
      ButtonVal = midi3note;

      Serial.print("arpeggio note:");
      Serial.print(midi3note, HEX);
      Serial.print(" arpeggio octave");
      Serial.println(midi3octave, HEX);
    }
    
  }

  // Callback for system exclusive messages
  void onSysExMessage(Parsing_MIDI_Interface &midi) override {
    SysExMessage se = midi.getSysExMessage();
    // Print the message
    Serial << F("System Exclusive message: ") << hex;
    for (size_t i = 0; i < se.length; ++i)
      Serial << se.data[i] << ' ';
    Serial << dec << F("on cable ") << se.CN << endl;
  }
} callbacks;

void readPots() {
  unsigned i;
  //a.setupArp(analogRead(baseNotepin), analogRead(baseOctavepin), analogRead(octaveShiftpin), analogRead(stepspin), analogRead(indelaypin), analogRead(orderpin), analogRead(modepin));
  a.setupArp(400, 400, 400, analogRead(stepspin), analogRead(indelaypin), analogRead(orderpin), analogRead(modepin));
  for (i = 20; i > 13; i--)     // pin 20,15,14,16 are connected to the four keys at the bottom ***FOR TEST ONLY, NOT IN ORDER***
    if (!(digitalRead(i))) {
      button_pressed = true;
      ButtonVal = 17 - i;
      return;
    }
  midi.update();                  // Continuously handle MIDI input
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  Serial.print("Serial debug enabled: ");

  FastLED.addLeds<NEOPIXEL, ledpin>(leds.data, leds.length);
  FastLED.setCorrection(TypicalPixelString);
  //Control_Surface.begin();      // Initialize Control Surface
  midi.begin(); // Initialize the MIDI interface
  midi.setCallbacks(callbacks); // Attach the custom callback
  Timer1.initialize(200000);          // initialize timer1, and set a 1/10 second period
  Timer1.attachInterrupt(readPots);   // We will read pots and buttons values within timer interrupt
  // Initialize pins for 2-pins pushbuttons with pullup enabled
  for (unsigned i = 14; i < 21; i++) { //14,15,16,20
    pinMode(i, INPUT_PULLUP);
  }
//  pinMode(testpin, INPUT_PULLUP);
//  pinMode(LEDPin, OUTPUT);  // LED pin
  button_pressed = false;
  ButtonVal = 1;
}


void loop() {
  if (button_pressed)
  {
    a.setProgression(ButtonVal - 1);
    button_pressed = false;
//    digitalWrite(LEDPin, HIGH);   // Switch on LED
    a.play();
//    digitalWrite(LEDPin, LOW);    // Switch off LED
  }
}


// Arrange the N elements of ARRAY in random order.
void shuffle(int *array, size_t n)
{
  if (n > 1)
  {
    size_t i;
    for (i = 0; i < n - 1; i++)
    {
      size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
      int t = array[j];
      array[j] = array[i];
      array[i] = t;
    }
  }
}

void arp::setupArp(short bn, short bo, short os, unsigned short st, unsigned int de, int od, unsigned mo)
{
  //baseNote = (notes) map(bn, 0, 800, 0, 11);
  //baseOctave = (short) map(bo, 0, 800, 0, 7);
  //octaveShift = (short) map(os, 0, 800, 0, 7);
  baseNote = midi4note;
  baseOctave = midi4octave;
  octaveShift = midi3octave;
  
  steps = (unsigned short) map(st, 0, 800, 1, 6);
  indelay = (unsigned int) map(de, 0, 800, 0, 500);
  mode = all_chords[(unsigned int) map(mo, 0, 800, 0, 8)];
  order = (unsigned int) map(od, 0, 800, 0, 4);

  leds[3] = CHSV(map(indelay, 0, 500, 0, 200), 255, 64);
  leds[2] = CHSV(map(mode, 0, 8, 0, 200), 255, 64);
  leds[1] = CHSV(map(order, 0, 4, 0, 200), 255, 64);
  leds[0] = CHSV(map(steps, 1, 6, 0, 200), 255, 64);
}

int arp::setProgression(unsigned int p)
{
  if (p < 8)
  {
    progression = p;
    return 0;
  }
  else
    return -1;
}

int* createChord(notes root, chord_types i, int *notes_array, unsigned short *sh1, unsigned short *sh2)
{
  *sh1 = 0; *sh2 = 0;
  int s1, s2;
  switch (i) {
    case maj:   s1 = 4; s2 = 7; break;
    case minor: s1 = 3; s2 = 7; break;
    case dim:   s1 = 3; s2 = 6; break;
    case aug:   s1 = 4; s2 = 8; break;
  }
  notes_array[0] = root;
  notes_array[1] = (root + s1) % 12;
  if (root + s1 > 11) *sh1 = 1; // octave shift
  notes_array[2] = (root + s2) % 12;
  if (root + s2 > 11) *sh2 = 1; // octave shift
}

short midiByNote (notes note, short octave)
{
  if ((octave < -1) || (octave > 8))
    return -1;

  return (octave + 1) * 12 + (int)note;
}

void arp::play()
{
  int i;
  short n, shift, oct_shift = 0, bn;
  unsigned short sh1, sh2, notes_added = 0;
  notes curNote[3];
  short notestoplay[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  // Play base note
  if ((int)baseNote + (int)mode[progression].Shift > 11)
    baseOctave++;
  i = (baseNote + mode[progression].Shift) % 12;
  bn = midiByNote ((notes)i, baseOctave);
  memset(notestoplay, sizeof(notestoplay), 0);

  // Create chord (notes root, intervals i, int *notes_array)
  createChord((notes)i, mode[progression].chord_type, (int*)curNote, &sh1, &sh2);

  // Create the progression
  if ((order == 0) || (order == 2) || (order == 3))
    for (i = 0; i < steps; i++)
    {
      shift = i % 3;
      oct_shift = i / 3;
      if (shift == 1)
        oct_shift += sh1;
      if (shift == 2)
        oct_shift += sh2;
      notestoplay[notes_added] = midiByNote (curNote[shift], octaveShift + oct_shift);
      notes_added++;
    }
  if ((order == 1) || (order == 2) || (order == 2))
    for (i = steps - 1; i >= 0; i--)
    {
      shift = i % 3;
      oct_shift = i / 3;
      if (shift == 1)
        oct_shift += sh1;
      if (shift == 2)
        oct_shift += sh2;
      notestoplay[notes_added] = midiByNote (curNote[shift], octaveShift + oct_shift);
      notes_added++;
    }

  if (order == 3)
    shuffle(notestoplay, notes_added);
  //Serial.print(bn); Serial.print("\r\n");
  midi.sendNoteOn({bn, CHANNEL_2}, 1);
  for (i = 0; i < notes_added; i++)
  {
    Serial.print(notestoplay[i]); Serial.print(" ");
    midi.sendNoteOn({notestoplay[i], CHANNEL_1}, 127);

#ifdef INT_SYNC
    // Delay value from poti
    delay(indelay);
#endif

#ifdef EXT_SYNC
    // Wait for click from sync in
    while ((digitalRead(syncinpin) == 0));
    delay(65);
#endif

    // Stop note
    midi.sendNoteOff({notestoplay[i], CHANNEL_1}, 0);    // Stop the note
  }

  //Stop base note
  midi.sendNoteOff({bn, CHANNEL_2}, 0);
}

arp::arp(notes bn, short bo, short os, unsigned short st, unsigned int d, unsigned m, unsigned int p) : baseNote(bn), baseOctave(bo), octaveShift(os), steps(st), indelay(d), progression(p)
{
  order = 0;
  mode = all_chords[m];
}

arp::arp()
{
  int i;
  baseNote = D;
  baseOctave = 5;
  octaveShift = -2;
  steps = 6;
  indelay = 200;
  progression = 0;
  mode = ionian;
}

void arp::midibegin()
{
  //  midi.begin(); // Initialize the MIDI interface
  //  midi.setCallbacks(callbacks); // Attach the custom callback
  //Serial.begin(57600);
}
