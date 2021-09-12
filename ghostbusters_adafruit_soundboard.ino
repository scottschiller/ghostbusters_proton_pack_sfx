#include <SoftwareSerial.h>
#include "Adafruit_Soundboard.h"

/**
 * "Ghostbusters" (1984) Proton Pack: "DIY" Sound Effects Module
 * (Arduino + Adafruit SFX Sound Board)
 * ---
 * Copyright (c) 2021, Scott Schiller
 * All rights reserved.
 * This source code is licensed under the BSD-style license found
 * in the LICENSE file in the root directory of this source tree. 
 * ---
 * 
 * Sounds on Adafruit Audio FX Sound Board (.WAV or .OGG, copied via USB or "flashed" via image)
 * 
 * T00: Power-up
 * T01: Ambient hum loop
 * T02: Shield on
 * T03: Fire start, burst/contain, and overheat
 * T04: Fire stop
 * T05: Shutdown
 * 
 * --- 2 MB vs. 16 MB Sound Board, .WAV vs. OGG size / latency ---
 * 
 * There are ~2 minutes of sound samples. The "ambient hum" can be shortened to save space.
 * I chose 16 MB, allowing CD-quality .WAV files in mono. (My proton pack has just one speaker.)
 * .OGG should fit into 2 MB, shortening the ambient hum loop sound if required.
 * 
 * There is some latency both in playback and looping in "trigger" mode.
 * https://learn.adafruit.com/adafruit-audio-fx-sound-board/triggering-audio
 * .WAV: ~120 ms playback, ~20 ms loop
 * .OGG: ~200 ms playback, ~120 ms loop
 * It's presumed that serial UART mode (e.g., Arduino driving sounds) is similar.
 *
 * --- Writing files to Adafruit Audio FX Sound Board (Micro USB -> computer) ---
 * 
 * NOTE: The device may not be recognized via USB if connected to other things.
 * Disconnect wires and remove the SFX board from any socket or breadboard before connecting USB.
 * BONUS NOTE: This thing can be *slow*. Format, copy etc. isn't frozen, just glacial. Be patient.
 * 
 * FORMAT: MS-DOS FAT (FAT16 or FAT12 if available?), Scheme: Master Boot Record
 * WRITING: Copy files, or "burn" an ISO-type disk image to the drive.
 * 
 * I've had occasional trouble with sounds playing after copying and/or renaming files a few times.
 * If this happens, it's best to start fresh: format/erase, or restore the drive from an image.
 * Reminder: Format/erase may take ~10 minutes.
 * 
 * Adafruit provides a "restore" disk image with the original file system, if needed.
 * They mention FAT12, but Mac OS reports "MS-DOS (FAT16)" once formatted.
 * https://learn.adafruit.com/adafruit-audio-fx-sound-board/downloads
 *
 * Device "flashing" can be done via GUI (OS X "Disk Utility"), or "Etcher" (free), or the terminal.
 * WARNING: CHOOSE YOUR TARGET DRIVE CAREFULLY. Make sure you are writing to the SFX board,
 * and not some hard drive with all your work. This process will destroy all existing disk contents.
 *
 * --- Making your own "restore" image (optional) ---
 * 
 * Once your sound board is set up, it's not a bad idea to make your own "restore" image.
 * OS X "Disk Utility" and similar apps should normally be able to create and restore images.
 * 
 * Disk Utility threw a permissions error for me, so I used the OS X Terminal:
 *  sudo dd if=/dev/disk## of=adafruit_sfx_card_gb_sounds.img bs=64k
 * 
 * NOTE: Replace /dev/disk## with the filesystem of the SFX card on your computer.
 * One way to find this: run df on the volume, by name. The default should be ADAFRUITSFX.
 *  df /Volumes/ADAFRUITSFX
 *  
 *  My results, below. Your results may vary.
 *  > Filesystem  512-blocks  Used Available Capacity iused ifree %iused  Mounted on
 *  > /dev/disk14      32416 27569      4847    86%     512     0  100%   /Volumes/ADAFRUITSFX
 *
 * Given the above - unmounting the volume, then creating the image:
 *  diskutil unmountDisk /dev/disk14
 *  sudo dd if=/dev/disk14 of=adafruit_sfx_card_gb_sounds.img bs=64k
 * 
 * Imaging has been between "slow" and "glacial" for my 16 MB version.
 *  > 16708608 bytes transferred in 48.393015 secs (345269 bytes/sec)
 *  > 16744448 bytes transferred in 170.931177 secs (97960 bytes/sec)
 * 
 */

/**
 * Debug levels:
 * 0 = disabled
 * 1 = info
 * 2 = verbose
 * 
 * This is handy when connected to the computer and troubleshooting functionality.
 * NOTE: Serial debug messages are at 115200 baud.
 */
const int DEBUG = 0;

// Sounds by offset, T00-T05.WAV
const int PACK_POWERUP = 0;
const int HUM_LOOP = 1;
const int SHIELD_ON = 2;
const int FIRE_START = 3;
const int FIRE_STOP = 4;
const int PACK_POWERDOWN = 5;

// Nice labels for debug messages
// Important: Match order as above from 0-5.
const String sounds[6] = {
  "PACK_POWERUP",
  "HUM_LOOP",
  "SHIELD_ON",
  "FIRE_START",
  "FIRE_STOP",
  "PACK_POWERDOWN"
};

// Arduino: Choose any two pins that can be used with SoftwareSerial to RX & TX
#define SFX_TX 5
#define SFX_RX 6

// Arduino pin -> Sound Board RST pin
#define SFX_RST 4

// Arduino pin -> Sound Board ACT pin
#define PIN_ACT 7

// Proton Pack power switch + fire button -> Arduino digital input pins
#define PIN_POWER_SWITCH 12
#define PIN_FIRE_BUTTON 11

// For input pins, switch / button + SFX. Note inverted values; labels are nicer to read in code.
#define PIN_OFF 1
#define PIN_ON 0
#define PIN_ACT_ACTIVE 0

// Help stabilize switch / button press inputs, and a general delay value.
#define DEBOUNCE_TIME 15

// Current and default "state" values.
// last_time is used as part of debounce.
bool pin_power_state = PIN_OFF;
bool pin_power_last_state = PIN_OFF;
unsigned long pin_power_last_time = millis();

bool pin_fire_state = PIN_OFF;
bool pin_fire_last_state = PIN_OFF;
unsigned long pin_fire_last_time = millis();

// General state tracking
bool is_playing = false;
bool is_board_active = false;
bool play_scheduled = false;
int last_played_sound;
int last_scheduled_sound;

// Proton pack state bits
bool state_power = false;
bool state_shield = false;
bool state_fired_at_least_once = false;

SoftwareSerial ss = SoftwareSerial(SFX_TX, SFX_RX);

// Arguments: software serial, debug port (N/A), reset pin
Adafruit_Soundboard sfx = Adafruit_Soundboard(&ss, NULL, SFX_RST);

// Can also try hardware serial...
// Adafruit_Soundboard sfx = Adafruit_Soundboard(&Serial1, NULL, SFX_RST);

void setup() {

  // Configure certain pins in pullup mode (pin -> GND vs. +5V), inverting values. LOW = 1, HIGH = 0.
  pinMode(PIN_ACT, INPUT_PULLUP);

  // Digital inputs: power switch, fire button.
  pinMode(PIN_POWER_SWITCH, INPUT_PULLUP);
  pinMode(PIN_FIRE_BUTTON, INPUT_PULLUP);

  // Serial debug @ 115200 baud
  if (DEBUG) initDebug();

  // softwareserial to SFX board, baud rate
  ss.begin(9600);

  if (sfx.reset()) {
    if (DEBUG) Serial.println("SFX board found! Waiting for power switch.");
  } else {
    if (DEBUG) Serial.println("SFX board not found? :( Check your wiring.");
    while (1);
  }

}

void initDebug() {
  /**
   * NOTE: Set Arduino IDE serial monitor to 115200 baud.
   * Otherwise, you won't be able to read debug messages.
   */
  Serial.begin(115200);

  char title1[] = "Arduino + Adafruit Audio FX Sound Board";
  char title2[] = "Ghostbusters Proton Pack, 2021 Edition ";

  // Ensure we start on a new line.
  Serial.println();

  // Nice box around text, because we're classy like that.
  debugTitleBox("╔", strlen(title1), "╗");
  debugTitleLine(title1);
  debugTitleLine(title2);
  debugTitleBox("╚", strlen(title1), "╝");
}

// Common serial debug output patterns

void debugTitleBox(String str1, int len, String str2) {
  // Full line, plus spacing on each side
  Serial.print(str1);
  for (int i=0; i<len + 2; i++) {
    Serial.print("═");
  }
  Serial.println(str2);
}

void debugTitleLine(String str) {
  Serial.print("║ ");
  Serial.print(str);
  Serial.println(" ║");
}

void debug(String str1, String str2) {
  Serial.print(str1);
  Serial.println(str2);
}

void debugBrackets(String str1, String str2) {
  Serial.print(str1);
  Serial.print("(");
  Serial.print(str2);
  Serial.println(")");
}

int debounceCheck(unsigned long last_time, unsigned long now) {
  // Ignore "noisy" inputs when they initially change, give time for value to stabilize
  const unsigned long delta = now - last_time;

  if (DEBUG > 1 && delta < DEBOUNCE_TIME) {
    debug("Debounce: Ignoring delta of ", String(delta));
  }

  return (now - last_time >= DEBOUNCE_TIME);
}

void maybeStopPlaying() {
  if (!is_playing) {
    if (DEBUG > 1) Serial.println("maybeStopPlaying(): No sound playing?");
    return;
  }

  if (DEBUG > 1) debugBrackets("maybeStopPlaying(): Stopping playback ", sounds[last_played_sound]);

  sfx.stop();
  is_playing = false;
}

void setIsPlayingAndDelay(bool play_scheduled) {
  if (!play_scheduled) return;

  is_playing = true;

  // Ensure SFX board PIN_ACT changes before the next loop.
  delay(DEBOUNCE_TIME);

  if (DEBUG > 1) debug("last_played_sound = ", sounds[last_scheduled_sound]);

  last_played_sound = last_scheduled_sound;
}

void play(int sound_offset) {
  // Any existing sound must be stopped before a new one starts.
  maybeStopPlaying();

  if (DEBUG) debugBrackets("play", sounds[sound_offset]);

  sfx.playTrack(sound_offset);

  // Flag that a sound is en route, so main loop knows.
  play_scheduled = true;
  last_scheduled_sound = sound_offset;
}

bool isBoardActive() {
  // Is the hardware currently playing a sound?
  return (digitalRead(PIN_ACT) == PIN_ACT_ACTIVE);
}

void setPower(bool power) {
  if (DEBUG) debugBrackets("setPower", String(power));

  state_power = power;

  // If powering off, reset some state.
  if (!power) {
    setShield(false);
    state_fired_at_least_once = false;
  }
}

void setShield(bool shield) {
  if (DEBUG) debugBrackets("setShield", String(shield));
  state_shield = shield;
}

void loop() {

  // Reset on each iteration.
  play_scheduled = false;

  // Is sound playing right now?
  is_board_active = isBoardActive();

  // Update local state if sound has ended.
  if (!is_board_active && is_playing) {
    if (DEBUG) debug("Playback ended: ", sounds[last_played_sound]);
    is_playing = false;
  }

  // Check switch + button inputs.
  pin_power_state = digitalRead(PIN_POWER_SWITCH);
  pin_fire_state = digitalRead(PIN_FIRE_BUTTON);

  // Track time for debounce purposes.
  const unsigned long now = millis();

  // POWER SWITCH changed: on/off.
  if (pin_power_state != pin_power_last_state && debounceCheck(pin_power_last_time, now)) {

    if (DEBUG) debug("\n⏻ POWER_SWITCH: ", pin_power_state == PIN_ON ? "ON ✓" : "OFF ✗");

    // Track status and timing
    pin_power_last_state = pin_power_state;
    pin_power_last_time = now;

    if (pin_power_state == PIN_ON) {
      setPower(true);
      play(PACK_POWERUP);
    } else {
      setPower(false);
      play(PACK_POWERDOWN);
    }

  }

  // FIRE BUTTON changed: "Shield up" (first press), "fire" (sequential presses.)
  if (state_power && pin_fire_state != pin_fire_last_state && debounceCheck(pin_fire_last_time, now)) {

    if (DEBUG) debug("\n⦿ FIRE BUTTON: ", pin_fire_state == PIN_ON ? "DOWN ↓" : "UP ↑");

    // Track status and timing
    pin_fire_last_state = pin_fire_state;
    pin_fire_last_time = now;

    if (!state_shield && pin_fire_state == PIN_ON) {

      // Shield up!
      if (DEBUG) Serial.println("Shield up ↑");

      play(SHIELD_ON);
      setShield(true);

    } else {

      // Power and shield up: fire start/stop.
      if (pin_fire_state == PIN_ON) {

        // First-time fire.
        // Loop (button held down) handled outside of this block.
        if (DEBUG > 1) Serial.println("Fire start");

        play(FIRE_START);
        state_fired_at_least_once = true;

      } else {

        /**
         * Button released.
         * Play "fire stop" if current sound is fire-related (start or loop).
         * Case handled here: button release after initial shield-up should not play anything else.
         * Button down/up after that point = fire start -> overheat -> restart power sequence.
         */
        if (state_fired_at_least_once && last_played_sound == FIRE_START) {
          if (DEBUG > 1) Serial.println("Fire stop");

          play(FIRE_STOP);
        }

      }
  
    }

  }

  /**
   * Firing -> overheat case: sound has completed, button still held down.
   * Reset internal state so pack will power up again.
   * Pardon the formatting of this `if` block for legibility.
   */
  if (
    state_power
    && state_fired_at_least_once
    && pin_fire_state == PIN_ON
    && debounceCheck(pin_fire_last_time, now)
    && (last_played_sound == FIRE_START)
    && !is_board_active && !play_scheduled
   ) {

    if (last_played_sound == FIRE_START) {
      // Overheat sequence has now played. restart pack.
      if (DEBUG) Serial.println("Pack has overheated, sound finished playing. Resetting, will restart.");

      // Pretend that power (and power switch) are off.
      state_power = false;

      // Reset power switch / pin.
      // On the next loop, the pack will reset and play PACK_POWERUP again.
      pin_power_last_state = PIN_OFF;

      // Manually reset fired state, too.
      state_fired_at_least_once = false;
    }

  }
  
  // If nothing scheduled to play, and not active, play idle background hum.
  if (state_power && !is_board_active && !play_scheduled) {
    play(HUM_LOOP);
  }

  // After all that, set flag and delay so PIN_ACT pin has time to switch over.
  setIsPlayingAndDelay(play_scheduled);

}
