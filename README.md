# RingoffireCC2DMX

Teensy 4.1 turns the **volcano sound from your Reaper master output** into a
ring-of-fire light tremor across **4x Cameo Thunder Wash 600 RGBW** fixtures.
Audio in ? tremor out. A Reaper plugin converts the master level to MIDI CC;
the Teensy shapes it into DMX.

```
Reaper master out
   |  JSFX plugin (audio -> MIDI CC)
   v  USB
Teensy 4.1  -- tremor engine (threshold/gain/decay/palette/rotation)
   |  Serial1 TX (pin 1)
   v
MAX3485 (3.3V) -> XLR
   |
   v  DMX daisy chain
Thunder Wash #1 (addr 1) -> #2 (8) -> #3 (15) -> #4 (22)
```

---

## Hardware

| Function | Teensy 4.1 pin | Notes |
|---|---|---|
| DMX TX | 1 (Serial1 TX) | -> MAX3485 DI |
| OLED SDA / SCL | 18 / 19 | SSD1306 128x32, addr 0x3C |
| Status LED | 13 | blinks on MIDI activity |
| MAX3485 DE+RE | tie to 3.3V | transmit-only |

MAX3485 is 3.3V native ? correct match for Teensy 4.1 (not 5V tolerant).
Set each Thunder Wash to **7-CH Mode_1**, DMX start addresses **1, 8, 15, 22**.

---

## The two pieces

### 1. Reaper plugin ? `reaper/ringoffire_audio2cc2.jsfx`

Put it on a track that receives the master mix. It reads the audio peak and
sends MIDI CC to the Teensy. Install: copy the file to
`%APPDATA%\REAPER\Effects\ringoffire\` and add it as a JS effect.

**Reaper routing (one-time):**
1. Preferences -> Audio -> MIDI Devices -> enable **Teensy MIDI** output.
2. Master track -> Route -> send audio to the plugin track.
3. Plugin track -> Route -> MIDI Hardware Output -> Teensy MIDI, channel 1.
4. Record-arm the track, set **Record: disable (input monitoring only)**,
   record-monitoring ON.

**Sliders (each sends a CC on channel 1 the moment you move it):**

| Slider | CC | Effect |
|---|---|---|
| Master brightness | CC1 | overall light level (0 = dark) |
| Threshold | CC3 | tremor only reacts above this |
| Gain | CC4 | reaction strength |
| Decay | CC5 | how fast light falls after a hit |
| Color heat | CC6 | deep red -> amber -> white-hot |
| Ring rotation | CC7 | tremor travels around the 4 heads (0 = in sync) |
| Peak strobe | CC8 | hardware strobe on hard peaks (0 = off) |
| Audio attack / release | ? | envelope feel (tight <-> long rumble) |
| Noise floor / Full scale | ? | dB window that maps to tremor 0-127 |

The bottom display shows the live audio bar, the tremor value being sent, and
8 bars (CC1..CC8) so you can verify what the Teensy should be receiving.

### 2. Teensy firmware ? `ringoffire_CC2DMX/ringoffire_CC2DMX.ino`

50 Hz tremor engine: instant attack + linear decay envelope, volcano palette,
per-fixture rotation from a history buffer. Builds with Arduino IDE +
Teensyduino, board **Teensy 4.1**, USB Type **Serial + MIDI**. Libraries:
Adafruit SSD1306, Adafruit GFX, TeensyDMX (qindesign).

Headless build + flash:

```powershell
arduino-cli compile --fqbn "teensy:avr:teensy41:usb=serialmidi,speed=600,opt=o2std,keys=en-us" ringoffire_CC2DMX
# then stage + reboot (arduino-cli upload alone is unreliable on Teensy):
teensy_post_compile -file=ringoffire_CC2DMX.ino -path=<build_dir> -tools=<teensy_tools> -board=TEENSY41
teensy_reboot
```

**OLED:** title + 4 live bars (F1-F4 = per-fixture intensity) + mini CC strip.
Top corners show raw CC2 (left) and CC1 (right). If no MIDI arrives for >1.5 s,
the title becomes a **yellow NO MIDI alarm banner**.

---

## MIDI CC -> DMX reference

**MIDI (from plugin, channel 1):**
CC1 master, CC2 tremor (audio), CC3 threshold, CC4 gain, CC5 decay,
CC6 heat, CC7 rotation, CC8 strobe.

**DMX (7-CH Mode_1 per fixture, start addrs 1/8/15/22):**

| Offset | Channel | Driven as |
|---|---|---|
| 1 | Master dimmer | envelope x master |
| 2 | Strobe | 0 (open); 128-250 on peaks if CC8 up |
| 3 | Red | dimmer |
| 4 | Green | dimmer x heat |
| 5 | Blue | 0 |
| 6 | White | flash on peaks (heat > 50%) |
| 7 | Sound | 0 (fixture mic off) |

Full fixture DMX table: `datasheets/CLTW600RGBW-dmx_control_table--D004114-en.pdf`.

---

## Files

- `ringoffire_CC2DMX/` ? Teensy 4.1 firmware
- `reaper/ringoffire_audio2cc2.jsfx` ? Reaper audio->CC plugin
- `datasheets/` ? Thunder Wash DMX control table

## Credits

Firmware structure based on VertigoCC2DMX by RVX (Victor Mazon Gardoqui), MIT.
Libraries: Adafruit, qindesign (TeensyDMX).
