// ============================================================================
// ringoffire_CC2DMX
// Teensy 4.1 — USB MIDI CC to DMX tremor engine for 4x Cameo Thunder Wash
// 600 RGBW (7-CH Mode_1), daisy-chained from DMX address 1
// (fixture start addresses: 1, 8, 15, 22).
//
// The lights tremble with the volcano sound of the master output:
// an audio envelope follower in Reaper (master out) sends MIDI CC2 to this
// device; the firmware applies threshold / gain / decay, drives a warm
// "ring of fire" color palette and rotates the tremor around the 4 heads.
//
// Board:   Teensy 4.1
// USB:     Tools > USB Type: "Serial + MIDI"
// Libs:    Adafruit SSD1306, Adafruit GFX, TeensyDMX (qindesign)
// DMX out: Serial1 TX (pin 1) -> MAX3485 module (3.3V!) -> XLR out
//          If the module exposes DE/RE pins, tie both to 3.3V (transmit).
// OLED:    SSD1306 128x32, I2C, SDA=18, SCL=19, addr 0x3C
//
// Based on VertigoCC2DMX by RVX (Victor Mazon Gardoqui), MIT License.
// ============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TeensyDMX.h>

// ---------------------------------------------------------------------------
// Firmware identity (splash screen + USB serial). Bump FW_VERSION each release.
// BUILD_STAMP auto-captures the compile date+time so the exact binary is
// identifiable later without trusting the manual date.
// ---------------------------------------------------------------------------
#define FW_NAME      "RingoffireCC2DMX"
#define FW_VERSION   "v1.1"
#define FW_DATE      "2026-09-28"
#define FW_COMMIT    "ecbc7d3"   // git short hash (update on each release)
#define FW_FEATURES  "4xTW600 7ch | CC2 tremor | NO-MIDI alarm"
#define BUILD_STAMP  __DATE__ " " __TIME__   // e.g. "Sep 28 2026 18:03:11"
#define SPLASH_MS    3000   // how long the welcome screen shows

// ---------------------------------------------------------------------------
// Fixtures & DMX layout (7-CH Mode_1 per fixture)
// ---------------------------------------------------------------------------
#define NUM_FIXTURES    4
#define FIXTURE_CHS     7
#define DMX_START       1                      // first fixture start address
#define DMX_CHANNELS    (NUM_FIXTURES * FIXTURE_CHS)   // 28

// 7-CH Mode_1 offsets within each fixture
#define CH_DIMMER   0   // Master dimmer 0-255
#define CH_STROBE   1   // 0-5 open | 128-250 strobe 1-30Hz | 251-255 open
#define CH_RED      2
#define CH_GREEN    3
#define CH_BLUE     4
#define CH_WHITE    5
#define CH_SOUND    6   // keep 0: sound control off (we react via MIDI)

// ---------------------------------------------------------------------------
// MIDI CC control mapping (all on any MIDI channel)
// ---------------------------------------------------------------------------
#define NUM_CC 8
// CC1 master brightness | CC2 tremor level (audio envelope from Reaper)
// CC3 threshold | CC4 gain/intensity | CC5 decay | CC6 color heat
// CC7 ring rotation | CC8 peak strobe
const uint8_t ccList[NUM_CC]   = { 1,   2,   3,   4,   5,   6,   7,   8  };
const char*   ccNames[NUM_CC]  = {"M", "TR", "TH", "GN", "DC", "HT", "RT", "ST"};
uint8_t       ccValues[128]    = {0};  // last raw CC value (0-127)

// ---------------------------------------------------------------------------
// Tremor engine state
// ---------------------------------------------------------------------------
#define ENV_HISTORY 64                  // power of two: ring buffer size
uint8_t  envHistory[ENV_HISTORY] = {0}; // past envelope values for rotation
uint8_t  envIndex = 0;
uint8_t  envelope = 0;                  // current tremor envelope 0-255
uint8_t  fixtureDim[NUM_FIXTURES] = {0}; // live dimmer per fixture (for OLED)

uint8_t dmxValues[DMX_CHANNELS + 1] = {0}; // 1-based

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64   // this module is 128x64 (SSD1306 / SSD1315)
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------------------------------------------------------------------
// DMX sender
// ---------------------------------------------------------------------------
#define DMX_SERIAL Serial1
qindesign::teensydmx::Sender dmx(DMX_SERIAL);

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static uint32_t ledOffTime  = 0;
static uint32_t lastTick    = 0;
static uint32_t lastFaderDraw = 0;
static uint32_t lastMidiMs  = 0;   // last time any mapped CC arrived
const  uint32_t ENGINE_TICK_MS      = 20;  // 50 Hz tremor engine
const  uint32_t FADER_DRAW_INTERVAL = 50;  // ms
const  uint32_t MIDI_TIMEOUT_MS     = 1500; // show NO MIDI after this silence

// ---------------------------------------------------------------------------
// OLED UI — title, 4 live fixture bars (the ring heads), mini CC strip
// ---------------------------------------------------------------------------
void drawFaders() {
  display.clearDisplay();

  // 1. Title: centered, full width (no clipping)
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  bool midiOk = (millis() - lastMidiMs <= MIDI_TIMEOUT_MS);
  if (midiOk) {
    const char* title = "RINGOFFIRE";
    int titleW = (int)strlen(title) * 6 - 1;   // 5px glyphs + 1px spacing
    display.setCursor((SCREEN_WIDTH - titleW) / 2, 0);
    display.print(title);
  } else {
    // Alarm: invert the whole yellow band (y0-15) with black text
    display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor((SCREEN_WIDTH - 42) / 2, 4);   // "NO MIDI" vertically centered in band
    display.print("NO MIDI");
    display.setTextColor(SSD1306_WHITE);
  }
  // Raw incoming values, for debugging the MIDI path:
  display.setCursor(0, 0);
  display.print(ccValues[2]);   // CC2 tremor level actually received (0-127)
  display.setCursor(SCREEN_WIDTH - 24, 0);
  display.print(ccValues[1]);   // CC1 master actually received (0-127)

  // 2. Four large bars: live intensity of each ring head (F1..F4)
  // Yellow band is y0-15; bars start below it so they never overlap it.
  const int barTop = 17;
  const int barBottom = SCREEN_HEIGHT - 11;     // room for labels below
  const int barH = barBottom - barTop;
  const int barW = 13, gap = 7, leftPad = 4;
  for (int f = 0; f < NUM_FIXTURES; f++) {
    int x = leftPad + f * (barW + gap);
    int h = map(fixtureDim[f], 0, 255, 0, barH);
    display.drawRect(x, barTop, barW, barH, SSD1306_WHITE);
    if (h > 0) display.fillRect(x + 2, barBottom - h, barW - 4, h, SSD1306_WHITE);
    display.setCursor(x + 1, barBottom + 2);    // F-label under each bar
    display.print('F');
    display.print(f + 1);
  }

  // 3. Mini CC strip (right side): 8 thin meters for M TR TH GN DC HT RT ST
  // Vertical divider separates the F1-F4 bars from the CC monitor strip
  const int stripX = leftPad + NUM_FIXTURES * (barW + gap) + 6;  // ~90
  display.drawLine(stripX - 3, barTop, stripX - 3, barBottom, SSD1306_WHITE);
  int mx = stripX;
  for (int i = 0; i < NUM_CC; i++) {
    int mh = map(ccValues[ccList[i]], 0, 127, 0, barH);
    if (mh > 0) display.fillRect(mx, barBottom - mh, 4, mh, SSD1306_WHITE);
    else        display.drawPixel(mx, barBottom - 1, SSD1306_WHITE);
    mx += 5;
  }

  display.display();
}

// ---------------------------------------------------------------------------
// Splash screen (version + date), shown for SPLASH_MS at startup
// ---------------------------------------------------------------------------
void showSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title: size 2 (16px tall), centered, upper area
  display.setTextSize(2);
  const char* title = "RINGOFFIRE";
  int tw = (int)strlen(title) * 12;
  if (tw > SCREEN_WIDTH) tw = SCREEN_WIDTH;
  display.setCursor((SCREEN_WIDTH - tw) / 2, 6);
  display.print(title);

  display.drawLine(8, 26, SCREEN_WIDTH - 9, 26, SSD1306_WHITE);

  // Info lines, size 1, well spaced (8px per line + gaps)
  display.setTextSize(1);
  char l1[28];
  snprintf(l1, sizeof(l1), "%s  %s", FW_VERSION, FW_DATE);
  display.setCursor((SCREEN_WIDTH - ((int)strlen(l1) * 6 - 1)) / 2, 32);
  display.print(l1);

  display.setCursor((SCREEN_WIDTH - ((int)strlen(BUILD_STAMP) * 6 - 1)) / 2, 42);
  display.print(BUILD_STAMP);

  // Board + commit hash (verifiable identity of the flashed binary)
  char l3[28];
  snprintf(l3, sizeof(l3), "Teensy 4.1  #%s", FW_COMMIT);
  display.setCursor((SCREEN_WIDTH - ((int)strlen(l3) * 6 - 1)) / 2, 54);
  display.print(l3);
  display.display();
}

// Machine-readable identity over USB serial. Open the Serial Monitor and the
// tagged lines can be grepped by you or by a script to verify what's flashed.
void printIdentity() {
  Serial.begin(115200);
  Serial.println(F("[FW] name=" FW_NAME));
  Serial.println(F("[FW] version=" FW_VERSION));
  Serial.println(F("[FW] date=" FW_DATE));
  Serial.println(F("[FW] commit=" FW_COMMIT));
  Serial.print(F("[FW] build="));  Serial.println(BUILD_STAMP);
  Serial.println(F("[FW] features=" FW_FEATURES));
  Serial.print(F("[FW] fixtures=")); Serial.println(NUM_FIXTURES);
  Serial.print(F("[FW] dmx_channels=")); Serial.println(DMX_CHANNELS);
}

// ---------------------------------------------------------------------------
// Critical error feedback (OLED + LED), halts
// ---------------------------------------------------------------------------
void showCriticalError(const char* msg) {
  pinMode(LED_BUILTIN, OUTPUT);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("CRITICAL ERROR!");
  display.println(msg);
  display.display();
  while (1) {
    digitalWrite(LED_BUILTIN, HIGH); delay(200);
    digitalWrite(LED_BUILTIN, LOW);  delay(200);
  }
}

// ---------------------------------------------------------------------------
// Tremor engine — runs at 50 Hz
// ---------------------------------------------------------------------------
void updateEngine() {
  // Read control CCs (0-127)
  uint16_t master = ccValues[1];                 // CC1 master brightness
  uint16_t level  = ccValues[2] * 2;             // CC2 audio envelope 0-254
  uint16_t thr    = ccValues[3] * 2;             // CC3 threshold 0-254
  uint16_t gain   = map(ccValues[4], 0, 127, 8, 128);  // CC4 gain (x0.125..x2, mid~1x)
  uint16_t decay  = map(ccValues[5], 0, 127, 12, 1);    // CC5 fall per tick
  uint16_t heat   = ccValues[6] * 2;             // CC6 color heat 0-254
  uint16_t rotate = map(ccValues[7], 0, 127, 0, 15);    // CC7 ticks offset/fixture
  uint16_t strobe = ccValues[8];                 // CC8 peak strobe amount

  // Envelope: instant attack, linear decay
  int32_t target = 0;
  if (level > thr) {
    target = ((int32_t)(level - thr) * gain) / 64;   // gain/64: 64 = unity
    if (target > 255) target = 255;
  }
  if ((int32_t)target > (int32_t)envelope) {
    envelope = (uint8_t)target;                  // attack: jump up
  } else if (envelope > decay) {
    envelope -= decay;                           // release: fall
  } else {
    envelope = 0;
  }

  // Store in history ring buffer (for the ring-rotation effect)
  envHistory[envIndex] = envelope;

  // Drive the 4 fixtures
  for (int f = 0; f < NUM_FIXTURES; f++) {
    // Each fixture reads the envelope N ticks in the past -> tremor
    // travels around the ring when CC7 > 0; 0 = all heads in sync.
    uint8_t envF = envHistory[(uint8_t)(envIndex - f * rotate) & (ENV_HISTORY - 1)];

    // Master dimmer = envelope scaled by master brightness
    uint16_t dim = ((uint16_t)envF * master) / 127;

    // Volcano palette: deep red base -> amber -> white-hot at peaks
    uint8_t r = (uint8_t)dim;
    uint8_t g = (uint8_t)(((uint16_t)dim * heat) / 255);
    uint8_t b = 0;
    uint8_t w = 0;
    if (heat > 128 && dim > 180) {
      w = (uint8_t)(((uint32_t)(dim - 180) * 255) / 75);  // white flash on peaks
    }

    // Strobe only on strong peaks, only if CC8 is up
    uint8_t strobeVal = 0;  // 0 = strobe open (required for clean wash)
    if (strobe > 10 && envF > 230) {
      strobeVal = (uint8_t)map(strobe, 0, 127, 128, 250);  // 1-30 Hz range
    }

    uint8_t base = DMX_START + f * FIXTURE_CHS;
    dmx.set(base + CH_DIMMER, (uint8_t)dim);
    dmx.set(base + CH_STROBE, strobeVal);
    dmx.set(base + CH_RED,    r);
    dmx.set(base + CH_GREEN,  g);
    dmx.set(base + CH_BLUE,   b);
    dmx.set(base + CH_WHITE,  w);
    dmx.set(base + CH_SOUND,  0);   // fixture mic always off
    // OLED shows the RAW envelope (pre-master) so the screen always proves
    // signal is arriving even if master brightness (CC1) is still 0.
    fixtureDim[f] = envF;
  }

  envIndex = (envIndex + 1) & (ENV_HISTORY - 1);
}

// ---------------------------------------------------------------------------
void setup() {
  // USB serial identity block (machine-readable, see printIdentity)
  printIdentity();

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (1) {  // OLED wiring fault: blink and halt
      digitalWrite(LED_BUILTIN, HIGH); delay(1000);
      digitalWrite(LED_BUILTIN, LOW);  delay(1000);
    }
  }
  // Welcome splash (version + date), then move on
  showSplash();
  delay(SPLASH_MS);

  dmx.begin();
  // Safe start: every channel at 0 (all fixtures dark, strobe open)
  for (int i = 1; i <= DMX_CHANNELS; i++) dmx.set(i, 0);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  drawFaders();
}

void loop() {
  // --- MIDI in: keep only the latest value per CC (flood protection) ---
  bool midiActive = false;
  while (usbMIDI.read()) {
    if (usbMIDI.getType() == usbMIDI.ControlChange) {
      byte control = usbMIDI.getData1();
      byte value   = usbMIDI.getData2();
      for (int i = 0; i < NUM_CC; i++) {
        if (control == ccList[i]) {
          ccValues[control] = value;
          midiActive = true;
          lastMidiMs = millis();   // feed the watchdog
          break;
        }
      }
    }
  }

  // --- Tremor engine tick (50 Hz) ---
  if (millis() - lastTick >= ENGINE_TICK_MS) {
    lastTick = millis();
    updateEngine();
  }

  // --- LED activity + OLED refresh ---
  if (midiActive) {
    digitalWrite(LED_BUILTIN, HIGH);
    ledOffTime = millis() + 50;
  }
  if (millis() - lastFaderDraw >= FADER_DRAW_INTERVAL) {
    lastFaderDraw = millis();
    drawFaders();
  }
  if (ledOffTime && millis() > ledOffTime) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOffTime = 0;
  }
}
