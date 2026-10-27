// Signal finder turntable: ESP8266 / ESP32 + MG995 270° servo.
//
// The servo signal pin is set per board in platformio.ini (GPIO4 on most
// boards, which is D2 on a Wemos D1 mini or NodeMCU).
//
// Serial 115200 baud, one command per line:
//   0..max   turn to that angle, e.g. "90" (max is 270 unless set with "m")
//   s<n>     speed in ms per degree, e.g. "s10" (3 = fastest, default 15)
//   m<n>     servo travel in degrees, e.g. "m180" (default 270, saved in flash)
//   r0 / r1  direction: r1 turns the other way (default r1, saved in flash)
//   ?        print position, target, speed, max and direction
//
// Replies:
//   MOVING <from> -> <to>   command accepted
//   AT <deg>                platform has stopped there (safe to measure signal)
//   ERR ...                 bad command, nothing changed

#include <Arduino.h>

#if defined(ESP8266)
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <Servo.h>
#else
#include <Preferences.h>
#endif

#ifndef SERVO_PIN
#define SERVO_PIN 4
#endif

// Servo travel: the angle MIN_US..MAX_US turns it through. 270 for a 270°
// MG995, 180 for the common 180° one. Set with "m<deg>", kept across restarts.
const int DEFAULT_MAX_DEG = 180;
const int MAX_DEG_LOW     = 10;
const int MAX_DEG_HIGH    = 360;

// Pulse widths for 0° and max. Most MG995s use 500..2500 us;
// if the ends don't line up with 0 and max, adjust these.
const int MIN_US = 500;
const int MAX_US = 2500;

// Which way angles turn. Set with "r0" / "r1", kept across restarts.
const bool DEFAULT_REVERSED = true;

const unsigned long MIN_MS_PER_DEG = 3;    // about the MG995's top speed
const unsigned long MAX_MS_PER_DEG = 1000;
const unsigned long SETTLE_MS = 300;       // wait after the last step before "AT"

int maxDeg = DEFAULT_MAX_DEG;
bool reversed = DEFAULT_REVERSED;
int currentDeg;  // both start at maxDeg / 2, the middle, set in setup()
int targetDeg;
unsigned long msPerDeg = 15;  // full 270° turn in ~4 s, gentle on the router
unsigned long lastStepMs = 0;
bool reported = false;

// --- Servo output ----------------------------------------------------------

#if defined(ESP8266)
Servo servo;

void servoBegin() {
  servo.attach(SERVO_PIN, MIN_US, MAX_US);
}

void servoWriteUs(int us) {
  servo.writeMicroseconds(us);
}
#else
// The ESP32 core has no Servo library; a 50 Hz LEDC PWM does the same job.
const uint32_t PWM_HZ   = 50;
const uint8_t  PWM_BITS = 14;  // the widest resolution every ESP32 variant has

void servoBegin() {
  ledcAttach(SERVO_PIN, PWM_HZ, PWM_BITS);
}

void servoWriteUs(int us) {
  const uint32_t periodUs = 1000000 / PWM_HZ;
  ledcWrite(SERVO_PIN, (uint32_t)us * (1 << PWM_BITS) / periodUs);
}
#endif

int degToUs(int deg) {
  if (reversed) deg = maxDeg - deg;
  return map(deg, 0, maxDeg, MIN_US, MAX_US);
}

// Change the travel without moving the platform: positions are rescaled so
// they still point at the same pulse widths.
void setMaxDeg(int deg) {
  currentDeg = (currentDeg * deg + maxDeg / 2) / maxDeg;
  targetDeg  = (targetDeg * deg + maxDeg / 2) / maxDeg;
  maxDeg = deg;
}

// Flip the direction without moving the platform: positions are mirrored so
// they still point at the same pulse widths.
void setReversed(bool r) {
  currentDeg = maxDeg - currentDeg;
  targetDeg  = maxDeg - targetDeg;
  reversed = r;
}

// --- Saved settings --------------------------------------------------------
// Survives restarts. Erasing the flash, or installing an ESP32 image from the
// web flasher, resets them to the defaults.

#if defined(ESP8266)
const uint16_t SETTINGS_MAGIC = 0x5346;  // tells our data from whatever was in flash

struct Settings {
  uint16_t magic;
  uint16_t maxDeg;
  uint8_t reversed;  // erased flash (0xFF) if saved before this field existed
};

void loadSettings(int &deg, int &rev) {
  EEPROM.begin(sizeof(Settings));
  Settings s;
  EEPROM.get(0, s);
  if (s.magic != SETTINGS_MAGIC) return;
  deg = s.maxDeg;
  rev = s.reversed;
}

void saveSettings() {
  EEPROM.put(0, Settings{SETTINGS_MAGIC, (uint16_t)maxDeg, (uint8_t)reversed});
  EEPROM.commit();
}
#else
Preferences prefs;

void loadSettings(int &deg, int &rev) {
  prefs.begin("turntable");
  deg = prefs.getUShort("max_deg", 0);
  rev = prefs.getUChar("reversed", 0xFF);
}

void saveSettings() {
  prefs.putUShort("max_deg", maxDeg);
  prefs.putUChar("reversed", reversed);
}
#endif

// --- Serial ports ----------------------------------------------------------

struct Port {
  Stream *stream;
  char line[16];
  uint8_t len;
  bool tooLong;
};

#if ARDUINO_USB_CDC_ON_BOOT
// Chips with native USB: the board's USB socket may go to the chip's own USB
// (Serial) or to a USB-UART bridge on UART0 (Serial0). Serve both.
Port ports[] = {{&Serial, {}, 0, false}, {&Serial0, {}, 0, false}};
#else
Port ports[] = {{&Serial, {}, 0, false}};
#endif

void say(const char *fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  for (Port &p : ports) p.stream->println(buf);
}

void handleLine(char *cmd) {
  // Trim spaces on both ends.
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  char *end = cmd + strlen(cmd);
  while (end > cmd && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
  if (*cmd == '\0') return;

  if (*cmd == '?') {
    say("POS %d TARGET %d SPEED %lu MAX %d REVERSE %d", currentDeg, targetDeg, msPerDeg, maxDeg, reversed);
    return;
  }

  if (*cmd == 'm' || *cmd == 'M') {
    long val = strtol(cmd + 1, &end, 10);
    if (end == cmd + 1 || *end != '\0' || val < MAX_DEG_LOW || val > MAX_DEG_HIGH) {
      say("ERR max must be m%d..m%d (degrees the servo turns)", MAX_DEG_LOW, MAX_DEG_HIGH);
      return;
    }
    if (val != maxDeg) {
      setMaxDeg(val);
      saveSettings();
    }
    say("MAX %d", maxDeg);
    return;
  }

  if (*cmd == 'r' || *cmd == 'R') {
    long val = strtol(cmd + 1, &end, 10);
    if (end == cmd + 1 || *end != '\0' || val < 0 || val > 1) {
      say("ERR reverse must be r0 or r1");
      return;
    }
    if (val != reversed) {
      setReversed(val);
      saveSettings();
    }
    say("REVERSE %d", reversed);
    return;
  }

  if (*cmd == 's' || *cmd == 'S') {
    long val = strtol(cmd + 1, &end, 10);
    if (end == cmd + 1 || *end != '\0' || val < (long)MIN_MS_PER_DEG || val > (long)MAX_MS_PER_DEG) {
      say("ERR speed must be s%lu..s%lu (ms per degree)", MIN_MS_PER_DEG, MAX_MS_PER_DEG);
      return;
    }
    msPerDeg = val;
    say("SPEED %lu", msPerDeg);
    return;
  }

  long val = strtol(cmd, &end, 10);
  if (end == cmd || *end != '\0' || val < 0 || val > maxDeg) {
    say("ERR angle must be a whole number 0..%d", maxDeg);
    return;
  }
  targetDeg = val;
  reported = false;
  say("MOVING %d -> %d", currentDeg, targetDeg);
}

void readPort(Port &p) {
  while (p.stream->available()) {
    char c = p.stream->read();
    if (c == '\n' || c == '\r') {
      p.line[p.len] = '\0';
      if (p.tooLong) say("ERR line too long");
      else handleLine(p.line);
      p.len = 0;
      p.tooLong = false;
    } else if (p.len < sizeof(p.line) - 1) {
      p.line[p.len++] = c;
    } else {
      p.tooLong = true;
    }
  }
}

// Step one degree at a time so the router on top doesn't get jerked around.
void updateServo() {
  unsigned long now = millis();
  if (currentDeg != targetDeg) {
    if (now - lastStepMs >= msPerDeg) {
      currentDeg += (targetDeg > currentDeg) ? 1 : -1;
      servoWriteUs(degToUs(currentDeg));
      lastStepMs = now;
    }
  } else if (!reported && now - lastStepMs >= SETTLE_MS) {
    say("AT %d", currentDeg);
    reported = true;
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif

#if defined(ESP8266)
  // The ESP8266 may come up as an access point from saved settings; keep its
  // radio quiet so it doesn't disturb the measurements. (ESP32 radios stay
  // off unless the sketch starts them.)
  WiFi.mode(WIFI_OFF);
#endif

  int savedMax = 0, savedRev = -1;
  loadSettings(savedMax, savedRev);
  if (savedMax >= MAX_DEG_LOW && savedMax <= MAX_DEG_HIGH) maxDeg = savedMax;
  if (savedRev == 0 || savedRev == 1) reversed = savedRev;
  currentDeg = targetDeg = maxDeg / 2;

  servoBegin();
  servoWriteUs(degToUs(currentDeg));
  lastStepMs = millis();

  say("");
  say("Turntable ready. Send 0..%d (degrees), s<ms/deg> for speed, m<deg> for max, r0/r1 for direction, ? for status.", maxDeg);
}

void loop() {
  for (Port &p : ports) readPort(p);
  updateServo();
}
