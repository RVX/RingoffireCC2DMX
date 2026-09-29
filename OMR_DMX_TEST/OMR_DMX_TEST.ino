// ============================================================================
// OMR_DMX_TEST
// Dead-simple hardware test for the ring-of-fire DMX chain.
// No MIDI, no Reaper. Just cycles all 4 Cameo Thunder Wash 600 RGBW through
// RED -> GREEN -> BLUE -> WHITE -> OFF in a loop so you can verify wiring,
// addressing, and the MAX3485 link. Also prints what it's doing over serial.
//
// Board: Teensy 4.1, USB Type: "Serial + MIDI" (serial used for debug only)
// Libs:  TeensyDMX (qindesign)
// DMX:   Serial1 TX (pin 1) -> MAX3485 DI, DE+RE tied to 3.3V
// ============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TeensyDMX.h>

// OLED (128x64, SSD1306/SSD1315, addr 0x3C, SDA=18 SCL=19)
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// 4 fixtures, 7 channels each (7-CH Mode_1), start addresses 1, 8, 15, 22
#define NUM_FIXTURES 4
#define FIXTURE_CHS  7
#define DMX_START    1

// channel offsets within a fixture (7-CH Mode_1)
#define CH_DIMMER 0   // master dimmer
#define CH_STROBE 1   // 0 = open (no strobe)
#define CH_RED    2
#define CH_GREEN  3
#define CH_BLUE   4
#define CH_WHITE  5
#define CH_SOUND  6   // 0 = mic off

#define STEP_MS 2000  // hold each color this long

qindesign::teensydmx::Sender dmx(Serial1);

// set one fixture to a color; dimmer full, strobe open, mic off
void setFixture(uint8_t f, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  uint8_t base = DMX_START + f * FIXTURE_CHS;
  dmx.set(base + CH_DIMMER, 255);
  dmx.set(base + CH_STROBE, 0);
  dmx.set(base + CH_RED,    r);
  dmx.set(base + CH_GREEN,  g);
  dmx.set(base + CH_BLUE,   b);
  dmx.set(base + CH_WHITE,  w);
  dmx.set(base + CH_SOUND,  0);
}

// set all 4 fixtures to the same color
void allColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  for (uint8_t f = 0; f < NUM_FIXTURES; f++) setFixture(f, r, g, b, w);
}

// show the current step on the OLED: big color name + step x/5
void showStep(const char* name, int idx, int total) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("OMR_DMX_TEST");                       // top band
  display.setCursor(SCREEN_WIDTH - 24, 0);
  display.print(idx); display.print("/"); display.print(total);
  display.setTextSize(3);                                // big color name
  int wpx = (int)strlen(name) * 18;                      // size3 ~ 18px/char
  display.setCursor((SCREEN_WIDTH - wpx) / 2, 24);
  display.print(name);
  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("OMR_DMX_TEST - cycling R G B W OFF on 4x Thunder Wash");
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OMR_DMX_TEST");
  display.println("starting...");
  display.display();
  dmx.begin();
  allColor(0, 0, 0, 0);   // start dark
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  struct { const char* name; uint8_t r, g, b, w; } steps[] = {
    {"RED",   255, 0,   0,   0  },
    {"GREEN", 0,   255, 0,   0  },
    {"BLUE",  0,   0,   255, 0  },
    {"WHITE", 0,   0,   0,   255},
    {"OFF",   0,   0,   0,   0  },
  };
  int total = sizeof(steps) / sizeof(steps[0]);
  for (int i = 0; i < total; i++) {
    auto& s = steps[i];
    Serial.print("-> "); Serial.println(s.name);
    digitalWrite(LED_BUILTIN, s.r || s.g || s.b || s.w ? HIGH : LOW);
    allColor(s.r, s.g, s.b, s.w);
    showStep(s.name, i + 1, total);
    delay(STEP_MS);
  }
}
