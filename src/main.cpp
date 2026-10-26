// Signal finder turntable: ESP8266 / ESP32 + MG995 270° servo.
//
// The servo signal pin is set per board in platformio.ini (GPIO4 on most
// boards, which is D2 on a Wemos D1 mini or NodeMCU).
//
// Serial 115200 baud, one command per line:
//   0..270   turn to that angle, e.g. "90"
//   s<n>     speed in ms per degree, e.g. "s10" (3 = fastest, default 15)
//   ?        print position, target and speed
//
// Replies:
//   MOVING <from> -> <to>   command accepted
//   AT <deg>                platform has stopped there (safe to measure signal)
//   ERR ...                 bad command, nothing changed

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <Servo.h>
#endif

#ifndef SERVO_PIN
#define SERVO_PIN 4
#endif

const int MAX_DEG   = 270;
const int START_DEG = 135;  // middle; the servo goes here on power-up

// Pulse widths for 0° and 270°. Most 270° MG995s use 500..2500 us;
// if the ends don't line up with 0 and 270, adjust these.
const int MIN_US = 500;
const int MAX_US = 2500;

const unsigned long MIN_MS_PER_DEG = 3;    // about the MG995's top speed
const unsigned long MAX_MS_PER_DEG = 1000;
const unsigned long SETTLE_MS = 300;       // wait after the last step before "AT"

int currentDeg = START_DEG;
int targetDeg  = START_DEG;
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
  return map(deg, 0, MAX_DEG, MIN_US, MAX_US);
}

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
  char buf[96];
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
    say("POS %d TARGET %d SPEED %lu", currentDeg, targetDeg, msPerDeg);
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
  if (end == cmd || *end != '\0' || val < 0 || val > MAX_DEG) {
    say("ERR angle must be a whole number 0..%d", MAX_DEG);
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

  servoBegin();
  servoWriteUs(degToUs(START_DEG));
  lastStepMs = millis();

  say("");
  say("Turntable ready. Send 0..%d (degrees), s<ms/deg> for speed, ? for status.", MAX_DEG);
}

void loop() {
  for (Port &p : ports) readPort(p);
  updateServo();
}
