/**
 * =============================================================================
 * POULTRY FEEDER — HX711 + ESP32-S3 + Servo + DS3231 RTC + Supabase
 * =============================================================================
 * HARDWARE:
 *   - ESP32-S3 microcontroller
 *   - HX711 load cell amplifier (DOUT=GPIO4, SCK=GPIO5)
 *   - MG946R continuous rotation servo (SIGNAL=GPIO6)
 *   - DS3231 RTC module (SDA=GPIO8, SCL=GPIO9)
 *   - Kill switch (GPIO2, INPUT_PULLUP)
 *
 * SERVO NOTES (CONTINUOUS ROTATION):
 *   - write(180) = spin forward
 *   - write(90)  = stop (dead band centre)
 *   - write(0)   = spin backward
 *   - FLAP_TRAVEL_MS controls how long to spin to reach 180 degrees
 *   - Every open must be matched with an equal close so flap position
 *     is always known — never leave the flap in an unknown position
 *
 * OFFLINE/ONLINE HYBRID MODE:
 *   - DS3231 RTC keeps accurate time without WiFi
 *   - Up to 8 feed times per day cached to NVS from Supabase when online
 *   - Ad-libitum threshold cached to NVS
 *   - Up to 50 feeding logs buffered in NVS, flushed to Supabase on reconnect
 *   - All dispensing logic works without WiFi
 *
 * JAM RECOVERY (3-strategy auto-retry):
 *   - Detection: bowl weight checked every 8 seconds during dispense
 *     The load cell under the bowl is the ONLY sensor — if bowl weight
 *     does not increase by 2g in 8 seconds, no flow is assumed
 *   - Attempt 1: reverse jog (300ms)     — clears flap outlet blockage
 *   - Attempt 2: rapid pulse pattern     — breaks bridging across cone neck
 *   - Attempt 3: aggressive wall-shake   — breaks wet clumps on cone walls
 *   - After all 3 fail: diagnose cause, alert app, stop motor
 *
 * HOPPER TRACKER (software estimate, no sensor needed):
 *   - Type REFILL <grams> after filling hopper to initialise
 *   - Every dispense subtracts from estimate automatically
 *   - Warns at 20% remaining, stops motor and alerts at 0
 *   - Estimate survives power cuts via NVS
 *
 * PREVENTION PULSE:
 *   - Every 4 hours a full open-close cycle agitates the cone
 *   - Uses FLAP_TRAVEL_MS so flap always returns to known closed position
 *   - Prevents food clumping on metal cone walls before it becomes a blockage
 *
 * BUG FIXES:
 *   - Bug 1 fixed: heartbeat PATCH in syncWithCloud() updates updated_at
 *     every sync cycle so the app correctly shows the device as online
 *   - Bug 2 fixed: all timestamps now include +03:00 EAT timezone suffix
 *     via getISOTimestamp() so Supabase stores and displays times correctly
 *
 * CHANGE LOG:
 *   - SAMPLES_NORMAL reduced from 20 to 8 for faster display reads
 *   - SAMPLES_NORMAL reduced from 8 to 5 — 425ms per read, still accurate with trim
 *   - HX711_MIN_MS reduced from 85 to 60 — safe at 10SPS, saves ~200ms per read
 *   - HX711_MIN_MS reduced from 60 to 50 — further inter-sample saving
 *   - HX711_MIN_MS restored to 80 — reliable conversion window for 10SPS mode
 *   - Weight read interval reduced from 2000ms to 800ms for snappier UI
 *   - Weight read interval reduced from 800ms to 400ms for snappier UI
 *   - HX711 not-ready now waits up to 150ms silently instead of spamming error
 *   - Live weight pushed to Supabase current_bowl_weight column every ~2s
 *     for real-time dashboard ring — only pushes when value changes > 0.5g
 * =============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "HX711.h"
#include <ESP32Servo.h>
#include <Preferences.h>
#include <Wire.h>
#include "RTClib.h"

// ─── WIFI / SUPABASE ──────────────────────────────────────────────────────────
const char* WIFI_SSID     = "me";
const char* WIFI_PASSWORD = "kimani254";
const char* SB_URL        = "https://agtdeofyqhifqsnvbsyt.supabase.co/rest/v1";
const char* SB_KEY        = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImFndGRlb2Z5cWhpZnFzbnZic3l0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzgwMDI3NDcsImV4cCI6MjA5MzU3ODc0N30.Au6VKGjQe-R5dCQJZ9vQRL-LIx0t0-EV_7PjswOXFWs";

// ─── PINS ─────────────────────────────────────────────────────────────────────
const int HX711_DOUT = 4;
const int HX711_SCK  = 5;
const int SERVO_PIN  = 6;
const int KILL_PIN   = 2;
const int RTC_SDA    = 8;
const int RTC_SCL    = 9;

// ─── SERVO POSITIONS ──────────────────────────────────────────────────────────
const int SERVO_OPEN  = 180;
const int SERVO_STOP  = 90;
const int SERVO_CLOSE = 0;

// ─── SERVO TIMING ─────────────────────────────────────────────────────────────
const int FLAP_TRAVEL_MS = 500;

// ─── JAM RECOVERY SETTINGS ───────────────────────────────────────────────────
const int JAM_MAX_RETRIES     = 3;
const int JAM_RETRY_DELAY_MS  = 2000;
const int JAM_JOG_MS          = 300;
const int BRIDGE_BREAK_PULSES = 4;
const int BRIDGE_PULSE_MS     = 120;

// ─── DIAGNOSIS THRESHOLDS ─────────────────────────────────────────────────────
const float HOPPER_EMPTY_DELTA_G = 5.0f;
const float JAM_BOWL_HIGH_G      = 100.0f;

// ─── PREVENTION PULSE INTERVAL ────────────────────────────────────────────────
const unsigned long PREVENTION_PULSE_INTERVAL = 4UL * 60UL * 60UL * 1000UL;

// ─── READING QUALITY SETTINGS ─────────────────────────────────────────────────
// SAMPLES_NORMAL = 5 — 425ms per read, still accurate with trim algorithm
const int   SAMPLES_NORMAL = 5;    // 425ms per read vs 680ms — still accurate with trim algorithm
const int   SAMPLES_CAL    = 30;
const float TRIM_PERCENT   = 0.15f;
const float NOISE_FLOOR_G  = 2.0f;
// CHANGE 2: HX711_MIN_MS restored to 80 — gives HX711 a reliable conversion
// window in 10SPS mode (true period ~100ms); 80ms inter-sample delay prevents
// reading a stale conversion before the next one is ready
const long  HX711_MIN_MS   = 80;   // HX711 at 10SPS — 80ms reliable conversion window

// ─── SCHEDULE CONFIG ──────────────────────────────────────────────────────────
const int MAX_FEED_TIMES   = 8;
const int MAX_OFFLINE_LOGS = 50;

// ─── NVS KEYS — CALIBRATION ───────────────────────────────────────────────────
const char* NVS_NAMESPACE  = "feeder";
const char* NVS_KEY_FACTOR = "cal_factor";
const char* NVS_KEY_OFFSET = "tare_offset";
const char* NVS_KEY_DONE   = "cal_done";

// ─── NVS KEYS — SCHEDULE CACHE ────────────────────────────────────────────────
const char* NVS_KEY_SLOT_COUNT = "slot_count";
const char* NVS_KEY_TARGET_G   = "target_g";
const char* NVS_KEY_THRESHOLD  = "adlib_thr";
const char* NVS_KEY_MODE       = "mode";
const char* NVS_KEY_SCHED_SET  = "sched_set";

// ─── NVS KEYS — OFFLINE LOG BUFFER ────────────────────────────────────────────
const char* NVS_KEY_LOG_COUNT = "log_count";

// ─── NVS KEYS — HOPPER TRACKER ────────────────────────────────────────────────
const char* NVS_KEY_HOPPER_G   = "hopper_g";
const char* NVS_KEY_HOPPER_SET = "hopper_set";

// ─── HARDWARE OBJECTS ─────────────────────────────────────────────────────────
HX711       scale;
Servo       feederServo;
Preferences prefs;
RTC_DS3231  rtc;

// ─── GLOBAL STATE ─────────────────────────────────────────────────────────────
String  configId        = "";
float   currentWeight   = 0.0f;
bool    isSystemEnabled = true;
bool    isJammed        = false;
bool    calDoneOnce     = false;
bool    rtcOK           = false;

// ─── HOPPER TRACKER STATE ─────────────────────────────────────────────────────
float hopperEstimatedGrams = -1.0f;
float hopperCapacityGrams  = 2000.0f;

// ─── CACHED SCHEDULE ──────────────────────────────────────────────────────────
struct FeedSlot {
  int  hour;
  int  minute;
  bool firedToday;
};

FeedSlot schedule[MAX_FEED_TIMES];
int      activeSlots    = 3;
float    schedTargetG   = 200.0f;
float    adlibThreshold = 500.0f;
String   feedMode       = "ad-libitum";
bool     scheduleSet    = false;

// ─── TIMING ───────────────────────────────────────────────────────────────────
unsigned long lastSyncTime        = 0;
unsigned long lastFlushTime       = 0;
unsigned long lastPreventionPulse = 0;

float lastPushedWeight = -1;

const unsigned long SYNC_INTERVAL  = 5000;
const unsigned long FLUSH_INTERVAL = 30000;

// ─── PROTOTYPES ───────────────────────────────────────────────────────────────
void     loadCalibrationFromNVS();
void     saveCalibrationToNVS(float factor, long offset);
void     loadScheduleFromNVS();
void     saveScheduleToNVS();
void     loadHopperEstimate();
void     saveHopperEstimate();
void     subtractFromHopper(float grams);
void     refillHopper(float grams);
void     diagnoseAndAlert(float totalDispensed, float bowlWeightAtStart);
void     preventionPulse();
void     runFullCalibration();
float    takeTrimmedReading(int n);
float    readRawTrimmed(int n);
float    readStableWeight();
void     syncWithCloud();
void     dispenseFeed(float gramsTarget);
void     reportJam();
void     logFeed(float dispensed, float bowl);
void     bufferLogOffline(float dispensed, float bowl);
void     flushOfflineLogs();
void     remoteTare();
void     remoteCalibrate(float knownGrams);
void     stopMotor();
bool     checkKillSwitch();
void     checkSchedule();
void     printSchedule();
DateTime getRTCTime();
void     printRTCTime();
String   getISOTimestamp();
String   httpGet(String ep);
String   httpPatch(String ep, String body);
String   httpPost(String ep, String body);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║  POULTRY FEEDER  —  STARTING UP  (OFFLINE-READY)  ║");
  Serial.println("╚════════════════════════════════════════════════════╝");

  for (int i = 0; i < MAX_FEED_TIMES; i++) {
    schedule[i] = {6 + i * 2, 0, false};
  }

  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) {
    Serial.println("[RTC] ERROR: DS3231 not found on SDA=8/SCL=9 — check wiring!");
    rtcOK = false;
  } else {
    rtcOK = true;
    if (rtc.lostPower()) {
      Serial.println("[RTC] Lost power — setting to compile timestamp as fallback.");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.print("[RTC] Time: ");
    printRTCTime();
  }

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  scale.begin(HX711_DOUT, HX711_SCK);
  Serial.print("[Scale] Waiting for HX711");
  int tries = 0;
  while (!scale.is_ready() && tries < 40) {
    delay(100); Serial.print("."); tries++;
  }
  Serial.println(scale.is_ready() ? " OK" : "\n[Scale] ERROR: HX711 not found!");

  loadCalibrationFromNVS();
  loadScheduleFromNVS();
  loadHopperEstimate();

  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_STOP);
  delay(200);
  feederServo.detach();

  pinMode(KILL_PIN, INPUT_PULLUP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  int wTries = 0;
  while (WiFi.status() != WL_CONNECTED && wTries < 30) {
    delay(500); Serial.print("."); wTries++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  else
    Serial.println("\n[WiFi] OFFLINE — running from cached schedule.");

  if (!calDoneOnce) {
    Serial.println("\n⚠️  CALIBRATION REQUIRED — Type H and press Enter.");
  } else {
    Serial.println("[Scale] Stability self-check (5s)...");
    float a = readStableWeight();
    delay(5000);
    float b = readStableWeight();
    float drift = fabsf(b - a);
    Serial.printf("[Scale] %s (drift=%.2fg)\n",
      drift > 2.0f ? "WARNING: unstable" : "Stable ✔", drift);
  }

  printSchedule();

  Serial.println("\n[Commands]");
  Serial.println("  H                   = Calibration wizard");
  Serial.println("  T                   = Tare (empty bowl)");
  Serial.println("  F                   = Show calibration factor");
  Serial.println("  D<g>                = Dispense grams  e.g. D200");
  Serial.println("  R                   = Raw ADC stream");
  Serial.println("  SCHED               = Show current schedule");
  Serial.println("  SET <S> HH MM       = Set slot time  e.g. SET 0 06 30");
  Serial.println("  SLOTS <n>           = Set active slot count  e.g. SLOTS 4");
  Serial.println("  TARGET <g>          = Set grams per feed  e.g. TARGET 250");
  Serial.println("  TIME                = Show RTC time");
  Serial.println("  SETTIME Y M D H M S = Set RTC  e.g. SETTIME 2025 6 1 8 0 0");
  Serial.println("  LOGS                = Show pending offline log count");
  Serial.println("  HOPPER              = Show estimated hopper level");
  Serial.println("  REFILL <g>          = Set hopper after refilling  e.g. REFILL 2000");
  Serial.println("  RESET               = Erase all NVS data\n");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {

  if (checkKillSwitch()) {
    stopMotor();
    Serial.println("[SAFETY] KILL SWITCH — halted.");
    delay(500);
    return;
  }

  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    String cu = cmd; cu.toUpperCase();

    if (cu == "H") {
      runFullCalibration();

    } else if (cu == "T") {
      remoteTare();

    } else if (cu == "F") {
      Serial.printf("[Factor] %.6f | Offset=%ld | NVS=%s\n",
        scale.get_scale(), scale.get_offset(),
        calDoneOnce ? "SAVED" : "NOT SET");

    } else if (cu.startsWith("D")) {
      float g = cu.substring(1).toFloat();
      if (g > 0) dispenseFeed(g);
      else Serial.println("Usage: D200");

    } else if (cu == "R") {
      Serial.println("RAW stream — press S to stop");
      while (true) {
        if (Serial.available() && toupper(Serial.read()) == 'S') break;
        unsigned long t = millis();
        while (!scale.is_ready() && millis()-t < 200);
        Serial.printf("Raw=%ld  Grams=%.2f\n",
          scale.read(), scale.get_units(1));
        delay(HX711_MIN_MS);
      }

    } else if (cu == "TIME") {
      printRTCTime();

    } else if (cu == "SCHED") {
      printSchedule();

    } else if (cu == "LOGS") {
      prefs.begin(NVS_NAMESPACE, true);
      int cnt = prefs.getInt(NVS_KEY_LOG_COUNT, 0);
      prefs.end();
      Serial.printf("[OfflineLogs] %d pending log(s) (max %d)\n",
                    cnt, MAX_OFFLINE_LOGS);

    } else if (cu == "HOPPER") {
      if (hopperEstimatedGrams < 0) {
        Serial.println("[Hopper] Not initialised — type REFILL <grams> after filling.");
      } else {
        float pct = hopperEstimatedGrams / hopperCapacityGrams * 100.0f;
        Serial.printf("[Hopper] Estimated: %.0fg remaining (%.0f%% of %.0fg capacity)\n",
                      hopperEstimatedGrams, pct, hopperCapacityGrams);
      }

    } else if (cu.startsWith("REFILL ")) {
      float g = cu.substring(7).toFloat();
      if (g > 0) refillHopper(g);
      else Serial.println("Usage: REFILL 2000");

    } else if (cu.startsWith("SET ")) {
      int slot = -1, hh = -1, mm = -1;
      sscanf(cmd.c_str() + 4, "%d %d %d", &slot, &hh, &mm);
      if (slot >= 0 && slot < MAX_FEED_TIMES &&
          hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
        schedule[slot].hour        = hh;
        schedule[slot].minute      = mm;
        schedule[slot].firedToday  = false;
        if (slot >= activeSlots) activeSlots = slot + 1;
        saveScheduleToNVS();
        Serial.printf("[Schedule] Slot %d → %02d:%02d  (active slots: %d)\n",
                      slot, hh, mm, activeSlots);
      } else {
        Serial.println("Usage: SET <slot 0-7> <HH 0-23> <MM 0-59>");
      }

    } else if (cu.startsWith("SLOTS ")) {
      int n = cu.substring(6).toInt();
      if (n >= 1 && n <= MAX_FEED_TIMES) {
        activeSlots = n;
        saveScheduleToNVS();
        Serial.printf("[Schedule] Active slots set to %d\n", n);
        printSchedule();
      } else {
        Serial.printf("Usage: SLOTS <1-%d>\n", MAX_FEED_TIMES);
      }

    } else if (cu.startsWith("TARGET ")) {
      float g = cu.substring(7).toFloat();
      if (g > 0) {
        schedTargetG = g;
        saveScheduleToNVS();
        Serial.printf("[Schedule] Target grams per feed = %.1fg\n", schedTargetG);
      } else {
        Serial.println("Usage: TARGET 250");
      }

    } else if (cu.startsWith("SETTIME ")) {
      int yr,mo,dy,hh,mm,ss;
      sscanf(cmd.c_str() + 8, "%d %d %d %d %d %d",
             &yr, &mo, &dy, &hh, &mm, &ss);
      if (rtcOK && yr > 2020) {
        rtc.adjust(DateTime(yr, mo, dy, hh, mm, ss));
        Serial.print("[RTC] Updated → ");
        printRTCTime();
      } else {
        Serial.println("Usage: SETTIME 2025 6 1 8 0 0");
      }

    } else if (cu == "RESET") {
      prefs.begin(NVS_NAMESPACE, false);
      prefs.clear();
      prefs.end();
      calDoneOnce          = false;
      scheduleSet          = false;
      hopperEstimatedGrams = -1.0f;
      scale.set_offset(0);
      scale.set_scale(1.0f);
      Serial.println("[NVS] All data erased. Type H to recalibrate.");
    }
  }

  static unsigned long lastHB = 0;
  if (millis() - lastHB > 10000) {
    lastHB = millis();
    if (rtcOK) printRTCTime();
    Serial.println("[Alive] Commands: H/T/F/D<g>/R/SCHED/SET/SLOTS/TARGET/"
                   "TIME/SETTIME/LOGS/HOPPER/REFILL/RESET");
  }

  // ── Weight reading ─────────────────────────────────────────────────────────
  // CHANGE 1: wait up to 150ms for HX711 silently instead of printing an error
  static unsigned long lastWT = 0;
  if (millis() - lastWT > 400) {
    lastWT = millis();

    // Wait up to 150ms for HX711 to finish its conversion cycle
    unsigned long tWait = millis();
    while (!scale.is_ready() && millis() - tWait < 150);
    if (scale.is_ready()) {
      currentWeight = readStableWeight();
      Serial.printf("[Weight] %.1fg | WiFi=%s | Mode=%s | Hopper=%.0fg | %s\n",
        currentWeight,
        WiFi.status() == WL_CONNECTED ? "OK" : "OFFLINE",
        feedMode.c_str(),
        hopperEstimatedGrams,
        isJammed ? "JAMMED" : (isSystemEnabled ? "ACTIVE" : "DISABLED"));

      if (WiFi.status() == WL_CONNECTED && configId != "" &&
          (fabsf(currentWeight - lastPushedWeight) > 0.5f || lastPushedWeight < 0)) {
        httpPatch("/feeder_config?id=eq." + configId,
                  "{\"current_bowl_weight\":" + String(currentWeight, 1) + "}");
        lastPushedWeight = currentWeight;
      }
    }
  }

  static unsigned long lastAdlib = 0;
  if (feedMode == "ad-libitum" && isSystemEnabled && !isJammed) {
    if (millis() - lastAdlib > 10000) {
      lastAdlib = millis();
      if (currentWeight < adlibThreshold) {
        float fill = max(50.0f, adlibThreshold - currentWeight);
        Serial.printf("[Ad-Lib] Bowl low (%.1fg < %.1fg) — filling %.1fg\n",
                      currentWeight, adlibThreshold, fill);
        dispenseFeed(fill);
      }
    }
  }

  if (rtcOK && feedMode == "scheduled" && isSystemEnabled && !isJammed) {
    checkSchedule();
  }

  if (isSystemEnabled && !isJammed) {
    if (millis() - lastPreventionPulse > PREVENTION_PULSE_INTERVAL) {
      lastPreventionPulse = millis();
      preventionPulse();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastSyncTime > SYNC_INTERVAL) {
      lastSyncTime = millis();
      syncWithCloud();
    }
    if (millis() - lastFlushTime > FLUSH_INTERVAL) {
      lastFlushTime = millis();
      flushOfflineLogs();
    }
  }

  if (!isSystemEnabled || isJammed) stopMotor();
}

// =============================================================================
// RTC HELPERS
// =============================================================================
DateTime getRTCTime() {
  return rtc.now();
}

void printRTCTime() {
  if (!rtcOK) { Serial.println("[RTC] Not available."); return; }
  DateTime n = rtc.now();
  Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d  Temp=%.1f°C\n",
    n.year(), n.month(), n.day(),
    n.hour(), n.minute(), n.second(),
    rtc.getTemperature());
}

// =============================================================================
// GET ISO TIMESTAMP — BUG 2 FIX
// =============================================================================
String getISOTimestamp() {
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  char ts[26];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d+03:00",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second());
  return String(ts);
}

// =============================================================================
// SCHEDULE DISPLAY
// =============================================================================
void printSchedule() {
  Serial.println("\n[Schedule] ─────────────────────────────────");
  Serial.printf("  Mode        : %s\n", feedMode.c_str());
  Serial.printf("  Active slots: %d / %d\n", activeSlots, MAX_FEED_TIMES);
  Serial.printf("  Target/feed : %.1fg\n", schedTargetG);
  Serial.printf("  Adlib thresh: %.1fg\n", adlibThreshold);
  for (int i = 0; i < activeSlots; i++) {
    Serial.printf("  Slot %d      : %02d:%02d  %s\n",
      i, schedule[i].hour, schedule[i].minute,
      schedule[i].firedToday ? "(fired today)" : "");
  }
  Serial.println("─────────────────────────────────────────────\n");
}

// =============================================================================
// SCHEDULE CHECK
// =============================================================================
void checkSchedule() {
  DateTime now = getRTCTime();
  int h = now.hour();
  int m = now.minute();
  int s = now.second();

  static int lastDay = -1;
  if (now.day() != lastDay) {
    lastDay = now.day();
    for (int i = 0; i < MAX_FEED_TIMES; i++) {
      schedule[i].firedToday = false;
    }
    Serial.println("[Schedule] New day — feed flags reset.");
  }

  for (int i = 0; i < activeSlots; i++) {
    if (h  == schedule[i].hour   &&
        m  == schedule[i].minute &&
        s  <  30 &&
        !schedule[i].firedToday) {
      Serial.printf("[Schedule] *** Slot %d firing at %02d:%02d"
                    " — dispensing %.0fg ***\n", i, h, m, schedTargetG);
      schedule[i].firedToday = true;
      dispenseFeed(schedTargetG);
    }
  }
}

// =============================================================================
// SCHEDULE NVS — LOAD
// =============================================================================
void loadScheduleFromNVS() {
  prefs.begin(NVS_NAMESPACE, true);
  scheduleSet = prefs.getBool(NVS_KEY_SCHED_SET, false);

  if (scheduleSet) {
    activeSlots    = prefs.getInt(NVS_KEY_SLOT_COUNT, 3);
    activeSlots    = constrain(activeSlots, 1, MAX_FEED_TIMES);
    schedTargetG   = prefs.getFloat(NVS_KEY_TARGET_G,  200.0f);
    adlibThreshold = prefs.getFloat(NVS_KEY_THRESHOLD, 500.0f);
    feedMode       = prefs.getString(NVS_KEY_MODE,      "ad-libitum");

    for (int i = 0; i < activeSlots; i++) {
      String kh = "ft_h_" + String(i);
      String km = "ft_m_" + String(i);
      schedule[i].hour       = prefs.getInt(kh.c_str(), schedule[i].hour);
      schedule[i].minute     = prefs.getInt(km.c_str(), schedule[i].minute);
      schedule[i].firedToday = false;
    }
  }
  prefs.end();

  Serial.printf("[NVS] Schedule: slots=%d mode=%s target=%.0fg threshold=%.0fg\n",
    activeSlots, feedMode.c_str(), schedTargetG, adlibThreshold);
}

// =============================================================================
// SCHEDULE NVS — SAVE
// =============================================================================
void saveScheduleToNVS() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool(NVS_KEY_SCHED_SET, true);
  prefs.putInt(NVS_KEY_SLOT_COUNT, activeSlots);
  prefs.putFloat(NVS_KEY_TARGET_G,  schedTargetG);
  prefs.putFloat(NVS_KEY_THRESHOLD, adlibThreshold);
  prefs.putString(NVS_KEY_MODE,     feedMode);

  for (int i = 0; i < activeSlots; i++) {
    String kh = "ft_h_" + String(i);
    String km = "ft_m_" + String(i);
    prefs.putInt(kh.c_str(), schedule[i].hour);
    prefs.putInt(km.c_str(), schedule[i].minute);
  }
  prefs.end();
  scheduleSet = true;
  Serial.printf("[NVS] Schedule saved (%d slots).\n", activeSlots);
}

// =============================================================================
// OFFLINE LOG BUFFER
// =============================================================================
void bufferLogOffline(float dispensed, float bowl) {
  prefs.begin(NVS_NAMESPACE, false);
  int count = prefs.getInt(NVS_KEY_LOG_COUNT, 0);

  if (count >= MAX_OFFLINE_LOGS) {
    Serial.println("[OfflineLog] Buffer full — dropping oldest entry.");
    for (int i = 0; i < count - 1; i++) {
      String val = prefs.getString(("log_" + String(i+1)).c_str(), "");
      prefs.putString(("log_" + String(i)).c_str(), val);
    }
    count = count - 1;
  }

  String tsStr = getISOTimestamp();

  String entry = "{\"amount_dispensed\":" + String(dispensed, 1) +
                 ",\"current_weight\":"   + String(bowl, 1) +
                 ",\"logged_at\":\""      + tsStr + "\"}";

  prefs.putString(("log_" + String(count)).c_str(), entry);
  prefs.putInt(NVS_KEY_LOG_COUNT, count + 1);
  prefs.end();
  Serial.printf("[OfflineLog] Buffered #%d  (total pending: %d)\n",
                count, count + 1);
}

// =============================================================================
// FLUSH OFFLINE LOGS TO SUPABASE
// =============================================================================
void flushOfflineLogs() {
  if (WiFi.status() != WL_CONNECTED) return;

  prefs.begin(NVS_NAMESPACE, false);
  int count = prefs.getInt(NVS_KEY_LOG_COUNT, 0);
  if (count == 0) { prefs.end(); return; }

  Serial.printf("[FlushLogs] Sending %d buffered log(s)...\n", count);
  int sent = 0;

  for (int i = 0; i < count; i++) {
    String entry = prefs.getString(("log_" + String(i)).c_str(), "");
    if (entry == "") { sent++; continue; }

    String result = httpPost("/feeding_logs", entry);
    if (result == "201" || result == "200") {
      sent++;
    } else {
      Serial.printf("[FlushLogs] Failed on log #%d (HTTP %s) — pausing.\n",
                    i, result.c_str());
      int remaining = count - i;
      for (int j = 0; j < remaining; j++) {
        String v = prefs.getString(("log_" + String(i+j)).c_str(), "");
        prefs.putString(("log_" + String(j)).c_str(), v);
      }
      prefs.putInt(NVS_KEY_LOG_COUNT, remaining);
      prefs.end();
      Serial.printf("[FlushLogs] Sent %d, %d still pending.\n", sent, remaining);
      return;
    }
  }

  prefs.putInt(NVS_KEY_LOG_COUNT, 0);
  prefs.end();
  Serial.printf("[FlushLogs] ✔ All %d log(s) sent.\n", sent);
}

// =============================================================================
// LOG FEED
// =============================================================================
void logFeed(float dispensed, float bowl) {
  String tsStr = getISOTimestamp();

  String body = "{\"amount_dispensed\":" + String(dispensed, 1) +
                ",\"current_weight\":"   + String(bowl, 1) +
                ",\"logged_at\":\""      + tsStr + "\"}";

  if (WiFi.status() == WL_CONNECTED) {
    flushOfflineLogs();
    String result = httpPost("/feeding_logs", body);
    if (result == "201" || result == "200") {
      Serial.println("[Log] Sent to Supabase ✔");
    } else {
      Serial.printf("[Log] POST failed (%s) — buffering.\n", result.c_str());
      bufferLogOffline(dispensed, bowl);
    }
  } else {
    bufferLogOffline(dispensed, bowl);
  }
}

// =============================================================================
// CLOUD SYNC
// =============================================================================
void syncWithCloud() {
  if (WiFi.status() != WL_CONNECTED) return;

  prefs.begin(NVS_NAMESPACE, false);
  String pendingCause = prefs.getString("pending_alert_cause", "");
  String pendingMsg   = prefs.getString("pending_alert_msg",   "");
  prefs.end();

  if (pendingCause != "" && configId != "") {
    String alertBody = "{\"jam_detected\":true,\"motor_enabled\":false,"
                       "\"alert_cause\":\"" + pendingCause + "\","
                       "\"alert_message\":\"" + pendingMsg + "\"}";
    String result = httpPatch("/feeder_config?id=eq." + configId, alertBody);
    if (result == "200" || result == "204") {
      prefs.begin(NVS_NAMESPACE, false);
      prefs.remove("pending_alert_cause");
      prefs.remove("pending_alert_msg");
      prefs.end();
      Serial.println("[Alert] Offline alert flushed to Supabase.");
    }
  }

  String raw = httpGet("/feeder_config?select=*&limit=1");
  if (raw == "") return;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, raw) || doc.size() == 0) return;
  JsonObject c = doc[0];

  configId = c["id"].as<String>();

  httpPatch("/feeder_config?id=eq." + configId,
            "{\"updated_at\":\"" + getISOTimestamp() + "\"}");

  String mode        = c["mode"]                  | "ad-libitum";
  String breed       = c["breed"]                 | "broiler";
  int    birds       = c["chicken_count"]         | 10;
  int    ageWeeks    = c["age_weeks"]             | 1;
  float  threshold   = c["adlib_threshold_grams"] | 500.0f;
  float  cloudFactor = c["calibration_factor"]    | 0.0f;
  bool   enabled     = c["motor_enabled"]         | true;
  bool   jammed      = c["jam_detected"]          | false;
  bool   tareTrig    = c["tare_trigger"]          | false;
  bool   calTrig     = c["calibration_trigger"]   | false;
  float  knownWt     = c["known_weight_grams"]    | 0.0f;

  if (rtcOK && rtc.lostPower()) {
    String ts = c["updated_at"] | "";
    if (ts.length() >= 19) {
      int yr = ts.substring(0,4).toInt();
      int mo = ts.substring(5,7).toInt();
      int dy = ts.substring(8,10).toInt();
      int hh = ts.substring(11,13).toInt();
      int mm = ts.substring(14,16).toInt();
      int ss = ts.substring(17,19).toInt();
      if (yr > 2020) {
        rtc.adjust(DateTime(yr, mo, dy, hh, mm, ss));
        Serial.println("[RTC] Synced from Supabase.");
      }
    }
  }

  if (!calDoneOnce && cloudFactor > 0) scale.set_scale(cloudFactor);

  if (tareTrig)             remoteTare();
  if (calTrig && knownWt>0) remoteCalibrate(knownWt);

  isSystemEnabled = enabled;
  isJammed        = jammed;

  float dailyPerBird = 0;
  String reqRaw = httpGet("/feed_requirements?breed=eq." + breed +
                          "&age_weeks=lte." + String(ageWeeks) +
                          "&order=age_weeks.desc&limit=1");
  if (reqRaw != "") {
    DynamicJsonDocument rd(512);
    if (!deserializeJson(rd, reqRaw) && rd.size() > 0)
      dailyPerBird = rd[0]["daily_feed_grams_per_bird"] | 0.0f;
  }
  if (dailyPerBird <= 0) dailyPerBird = 30.0f + max(0, ageWeeks-1) * 25.0f;

  float cloudTarget = c["target_dispense_grams"] | 0.0f;
  float newTarget   = (cloudTarget > 0)
                        ? cloudTarget
                        : (birds * dailyPerBird) / (float)activeSlots;

  int cloudSlots = c["slot_count"] | activeSlots;
  cloudSlots = constrain(cloudSlots, 1, MAX_FEED_TIMES);

  bool schedChanged = false;

  if (cloudSlots != activeSlots) {
    activeSlots  = cloudSlots;
    schedChanged = true;
  }

  for (int i = 0; i < activeSlots; i++) {
    char colName[16];
    snprintf(colName, sizeof(colName), "feed_time_%d", i);
    String t = c[colName] | "";
    if (t.length() >= 5) {
      int nh = t.substring(0,2).toInt();
      int nm = t.substring(3,5).toInt();
      if (nh >= 0 && nh < 24 && nm >= 0 && nm < 60) {
        if (nh != schedule[i].hour || nm != schedule[i].minute) {
          schedule[i].hour        = nh;
          schedule[i].minute      = nm;
          schedule[i].firedToday  = false;
          schedChanged = true;
        }
      }
    }
  }

  bool paramsChanged = (fabsf(newTarget   - schedTargetG)   > 0.5f ||
                        fabsf(threshold   - adlibThreshold) > 0.5f ||
                        mode != feedMode);

  if (schedChanged || paramsChanged) {
    schedTargetG   = newTarget;
    adlibThreshold = threshold;
    feedMode       = mode;
    saveScheduleToNVS();
    Serial.println("[Sync] Schedule updated and cached to NVS.");
    printSchedule();
  }

  Serial.printf("[Sync] Mode=%s Breed=%s Birds=%d Age=%dwk Bowl=%.1fg"
                " Thresh=%.1fg Target=%.1fg Slots=%d Hopper=%.0fg\n",
    mode.c_str(), breed.c_str(), birds, ageWeeks,
    currentWeight, threshold, schedTargetG, activeSlots, hopperEstimatedGrams);

  if (!isSystemEnabled || isJammed) {
    Serial.printf("[Sync] Blocked — enabled=%d jammed=%d\n",
                  isSystemEnabled, isJammed);
    return;
  }

  if (mode == "scheduled") {
    bool schedTrig = c["scheduled_feed_trigger"] | false;
    if (schedTrig) {
      Serial.println("[Sync] One-shot trigger from cloud.");
      dispenseFeed(schedTargetG);
      httpPatch("/feeder_config?id=eq." + configId,
                "{\"scheduled_feed_trigger\":false}");
    }
  }
}

// =============================================================================
// DISPENSE FEED
// =============================================================================
void dispenseFeed(float gramsTarget) {
  if (gramsTarget <= 0) return;

  float startW = readStableWeight();
  float goalW  = startW + gramsTarget;
  Serial.printf("[Dispense] Start=%.1fg  +%.1fg  Goal=%.1fg\n",
                startW, gramsTarget, goalW);

  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_OPEN);
  Serial.println("[Servo] Opening flap — spinning forward");
  delay(FLAP_TRAVEL_MS);

  feederServo.write(SERVO_STOP);
  Serial.println("[Servo] Holding open — dispensing...");

  unsigned long tStart     = millis();
  unsigned long tLastPrint = 0;
  unsigned long tJamCheck  = millis();
  float wAtJamChk          = startW;
  bool  success            = false;

  while (millis() - tStart < 60000) {

    if (checkKillSwitch()) {
      Serial.println("[SAFETY] Kill switch during dispense — stopping!");
      break;
    }

    float w = readStableWeight();

    if (millis() - tLastPrint > 500) {
      tLastPrint = millis();
      Serial.printf("  %.1fg / %.1fg (%.0f%%)\n",
        w, goalW,
        min(100.0f, (w - startW) / gramsTarget * 100.0f));
    }

    if (w >= goalW) {
      success = true;
      Serial.println("[Dispense] Target reached!");
      break;
    }

    if (millis() - tJamCheck >= 8000) {
      tJamCheck = millis();

      if ((w - wAtJamChk) < 2.0f) {
        bool cleared = false;

        for (int attempt = 1; attempt <= JAM_MAX_RETRIES; attempt++) {
          Serial.printf("[JAM] No flow detected — attempt %d of %d\n",
                        attempt, JAM_MAX_RETRIES);

          if (attempt == 1) {
            Serial.println("[JAM] Strategy 1: reverse jog — flap outlet");
            feederServo.write(SERVO_CLOSE);
            delay(JAM_JOG_MS);
            feederServo.write(SERVO_STOP);

          } else if (attempt == 2) {
            Serial.println("[JAM] Strategy 2: bridge-break pulses — cone neck");
            for (int p = 0; p < BRIDGE_BREAK_PULSES; p++) {
              feederServo.write(SERVO_OPEN);
              delay(BRIDGE_PULSE_MS);
              feederServo.write(SERVO_CLOSE);
              delay(BRIDGE_PULSE_MS);
            }
            feederServo.write(SERVO_STOP);

          } else {
            Serial.println("[JAM] Strategy 3: aggressive wall-shake — cone walls");
            for (int p = 0; p < 6; p++) {
              feederServo.write(SERVO_OPEN);
              delay(200);
              feederServo.write(SERVO_CLOSE);
              delay(200);
            }
            feederServo.write(SERVO_STOP);
          }

          delay(JAM_RETRY_DELAY_MS);

          feederServo.write(SERVO_OPEN);
          delay(600);
          float wBefore = readStableWeight();
          delay(2000);
          float wAfter  = readStableWeight();

          if ((wAfter - wBefore) >= 2.0f) {
            Serial.printf("[JAM] Cleared on attempt %d — flow=+%.1fg. Resuming.\n",
                          attempt, wAfter - wBefore);
            cleared   = true;
            tJamCheck = millis();
            wAtJamChk = wAfter;
            w         = wAfter;
            break;
          }

          Serial.printf("[JAM] Attempt %d still blocked (delta=%.1fg)\n",
                        attempt, wAfter - wBefore);
        }

        if (!cleared) {
          Serial.printf("[JAM] All %d attempts failed — closing and diagnosing.\n",
                        JAM_MAX_RETRIES);

          feederServo.write(SERVO_CLOSE);
          delay(FLAP_TRAVEL_MS);
          feederServo.write(SERVO_STOP);
          delay(100);
          feederServo.detach();

          float dispensedSoFar = max(0.0f, readStableWeight() - startW);
          logFeed(dispensedSoFar, readStableWeight());
          subtractFromHopper(dispensedSoFar);
          diagnoseAndAlert(dispensedSoFar, startW);

          isJammed = true;
          return;
        }

      } else {
        wAtJamChk = w;
      }
    }

    delay(50);
  }

  if (!success && !isJammed) Serial.println("[Dispense] Timeout — 60s limit reached.");

  Serial.println("[Servo] Closing flap — spinning backward");
  feederServo.write(SERVO_CLOSE);
  delay(FLAP_TRAVEL_MS);
  feederServo.write(SERVO_STOP);
  delay(100);
  feederServo.detach();
  Serial.println("[Servo] Flap closed — back at starting position.\n");

  float finalW    = readStableWeight();
  float dispensed = max(0.0f, finalW - startW);

  logFeed(dispensed, finalW);
  subtractFromHopper(dispensed);
}

// =============================================================================
// PREVENTION PULSE
// =============================================================================
void preventionPulse() {
  if (isJammed || !isSystemEnabled) return;
  Serial.println("[Prevention] Agitation pulse — keeping cone walls clear.");

  feederServo.attach(SERVO_PIN, 500, 2400);

  feederServo.write(SERVO_OPEN);
  delay(FLAP_TRAVEL_MS);
  feederServo.write(SERVO_STOP);
  delay(300);

  feederServo.write(SERVO_CLOSE);
  delay(FLAP_TRAVEL_MS);
  feederServo.write(SERVO_STOP);
  delay(300);

  feederServo.write(SERVO_OPEN);
  delay(FLAP_TRAVEL_MS);
  feederServo.write(SERVO_STOP);
  delay(300);

  feederServo.write(SERVO_CLOSE);
  delay(FLAP_TRAVEL_MS);
  feederServo.write(SERVO_STOP);
  delay(100);

  feederServo.detach();
  Serial.println("[Prevention] Done — flap returned to closed position.");
}

// =============================================================================
// DIAGNOSE AND ALERT
// =============================================================================
void diagnoseAndAlert(float totalDispensed, float bowlWeightAtStart) {
  String cause   = "";
  String message = "";

  if (totalDispensed < HOPPER_EMPTY_DELTA_G &&
      bowlWeightAtStart < JAM_BOWL_HIGH_G) {
    cause   = "hopper_empty";
    message = "Hopper may be empty — only " + String(totalDispensed, 1) +
              "g dispensed across all retries. Please refill the hopper.";

  } else if (totalDispensed < HOPPER_EMPTY_DELTA_G &&
             bowlWeightAtStart >= JAM_BOWL_HIGH_G) {
    cause   = "jam";
    message = "Jam detected — bowl was already " +
              String(bowlWeightAtStart, 1) +
              "g full and nothing moved. Check cone neck and flap outlet.";

  } else {
    cause   = "partial_jam_or_low_hopper";
    message = "Partial blockage or low hopper — dispensed only " +
              String(totalDispensed, 1) +
              "g before stopping. Check hopper level and outlet.";
  }

  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║  ALERT: " + message);
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");

  if (WiFi.status() == WL_CONNECTED && configId != "") {
    String body = "{\"jam_detected\":true,"
                  "\"motor_enabled\":false,"
                  "\"alert_cause\":\"" + cause + "\","
                  "\"alert_message\":\"" + message + "\"}";
    httpPatch("/feeder_config?id=eq." + configId, body);
    Serial.println("[Alert] Pushed to Supabase — cause=" + cause);
  } else {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("pending_alert_cause", cause);
    prefs.putString("pending_alert_msg",   message);
    prefs.end();
    Serial.println("[Alert] Offline — saved to NVS, will push on reconnect.");
  }
}

// =============================================================================
// HOPPER TRACKER — LOAD FROM NVS
// =============================================================================
void loadHopperEstimate() {
  prefs.begin(NVS_NAMESPACE, true);
  bool set = prefs.getBool(NVS_KEY_HOPPER_SET, false);
  if (set) {
    hopperEstimatedGrams = prefs.getFloat(NVS_KEY_HOPPER_G, -1.0f);
    Serial.printf("[Hopper] Loaded estimate: %.0fg remaining\n",
                  hopperEstimatedGrams);
  } else {
    hopperEstimatedGrams = -1.0f;
    Serial.println("[Hopper] Not initialised — type REFILL <grams> after filling.");
  }
  prefs.end();
}

// =============================================================================
// HOPPER TRACKER — SAVE TO NVS
// =============================================================================
void saveHopperEstimate() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat(NVS_KEY_HOPPER_G,  hopperEstimatedGrams);
  prefs.putBool(NVS_KEY_HOPPER_SET, true);
  prefs.end();
}

// =============================================================================
// HOPPER TRACKER — SUBTRACT AFTER DISPENSE
// =============================================================================
void subtractFromHopper(float grams) {
  if (hopperEstimatedGrams < 0) return;

  hopperEstimatedGrams -= grams;
  if (hopperEstimatedGrams < 0) hopperEstimatedGrams = 0;
  saveHopperEstimate();

  Serial.printf("[Hopper] Subtracted %.0fg — estimated remaining: %.0fg\n",
                grams, hopperEstimatedGrams);

  float warnLevel = hopperCapacityGrams * 0.20f;
  if (hopperEstimatedGrams > 0 && hopperEstimatedGrams <= warnLevel) {
    Serial.printf("[Hopper] WARNING — only %.0fg left (%.0f%% of capacity)\n",
                  hopperEstimatedGrams,
                  hopperEstimatedGrams / hopperCapacityGrams * 100.0f);
    if (WiFi.status() == WL_CONNECTED && configId != "") {
      String body = "{\"alert_cause\":\"hopper_low\","
                    "\"alert_message\":\"Hopper is low — approximately " +
                    String(hopperEstimatedGrams, 0) +
                    "g remaining. Please refill soon.\"}";
      httpPatch("/feeder_config?id=eq." + configId, body);
    }
  }

  if (hopperEstimatedGrams <= 0) {
    Serial.println("[Hopper] ESTIMATED EMPTY — alerting and stopping motor.");
    if (WiFi.status() == WL_CONNECTED && configId != "") {
      String body = "{\"alert_cause\":\"hopper_empty\","
                    "\"alert_message\":\"Hopper is empty. Please refill now.\","
                    "\"motor_enabled\":false}";
      httpPatch("/feeder_config?id=eq." + configId, body);
    }
  }
}

// =============================================================================
// HOPPER TRACKER — REFILL
// =============================================================================
void refillHopper(float grams) {
  hopperEstimatedGrams = grams;
  saveHopperEstimate();
  Serial.printf("[Hopper] Refilled — now tracking %.0fg\n", grams);

  if (WiFi.status() == WL_CONNECTED && configId != "") {
    String body = "{\"jam_detected\":false,"
                  "\"motor_enabled\":true,"
                  "\"alert_cause\":\"\","
                  "\"alert_message\":\"\"}";
    httpPatch("/feeder_config?id=eq." + configId, body);
    Serial.println("[Hopper] Supabase alerts cleared — system re-enabled.");
  }
}

// =============================================================================
// CALIBRATION WIZARD
// =============================================================================
void runFullCalibration() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         CALIBRATION WIZARD             ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  Serial.println("STEP 1/5 — EMPTY THE PLATFORM. Press Enter...");
  while (!Serial.available()) {
    if (checkKillSwitch()) { Serial.println("Cancelled."); return; }
    delay(10);
  }
  Serial.readString();

  scale.set_scale(1.0f); scale.set_offset(0); delay(500);
  Serial.println("Sampling raw zero (30 readings)...");
  long rawZero = (long)readRawTrimmed(30);
  scale.set_offset(rawZero);
  Serial.printf("Raw zero = %ld counts\n\n", rawZero);

  Serial.println("STEP 2/5 — PLACE KNOWN WEIGHT (100-5000g).");
  Serial.println("Type grams and press Enter:");
  while (!Serial.available()) {
    if (checkKillSwitch()) { Serial.println("Cancelled."); return; }
    delay(10);
  }
  float knownGrams = Serial.readStringUntil('\n').toFloat();
  if (knownGrams < 100 || knownGrams > 5000) {
    Serial.println("ERROR: 100-5000g only."); return;
  }

  Serial.println("\nSTEP 3/5 — SAMPLING (5s settle)...");
  for (int i = 5; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
  long rawWith = (long)readRawTrimmed(SAMPLES_CAL);
  long netRaw  = rawWith - rawZero;
  Serial.printf("Net raw = %ld counts\n", netRaw);
  if (abs(netRaw) < 1000) {
    Serial.println("ERROR: reading too small. Check wiring/weight."); return;
  }

  float newFactor = (float)netRaw / knownGrams;
  bool flipped = (newFactor < 0); if (flipped) newFactor = -newFactor;
  scale.set_offset(rawZero); scale.set_scale(newFactor);

  Serial.println("\nSTEP 4/5 — VERIFICATION (weight still on platform)...");
  delay(1000);
  float sum = 0;
  for (int i = 0; i < 5; i++) {
    float r = fabsf(takeTrimmedReading(10));
    sum += r;
    Serial.printf("  Reading %d: %.2fg\n", i+1, r);
    delay(500);
  }
  float avg = sum / 5.0f;
  float err = fabsf(avg - knownGrams) / knownGrams * 100.0f;
  Serial.printf("  Avg=%.2fg  Error=%.2f%%  Flip=%s\n",
                avg, err, flipped ? "YES" : "no");
  if (err > 3.0f) {
    Serial.println("⚠ >3% error. Enter to continue or H to retry.");
    while (!Serial.available()) delay(10);
    String ch = Serial.readStringUntil('\n'); ch.trim(); ch.toUpperCase();
    if (ch == "H") { runFullCalibration(); return; }
  } else {
    Serial.println("✔ PASSED");
  }

  Serial.println("\nSTEP 5/5 — FINAL ZERO. REMOVE weight. Press Enter...");
  while (!Serial.available()) {
    if (checkKillSwitch()) return;
    delay(10);
  }
  Serial.readString();
  Serial.println("Settling 5s...");
  for (int i = 5; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }

  scale.set_scale(1.0f); scale.set_offset(0); delay(300);
  long finalZero = (long)readRawTrimmed(30);
  scale.set_offset(finalZero); scale.set_scale(newFactor); delay(500);

  float zc = readStableWeight();
  Serial.printf("Zero check: %.2fg\n", zc);
  if (fabsf(zc) > 2.0f) {
    Serial.println("Re-taring...");
    scale.set_scale(1.0f); scale.set_offset(0); delay(300);
    long rz = (long)readRawTrimmed(30);
    scale.set_offset(rz); scale.set_scale(newFactor); delay(500);
    finalZero = rz;
  }

  saveCalibrationToNVS(newFactor, finalZero);
  if (configId != "" && WiFi.status() == WL_CONNECTED) {
    String body = "{\"calibration_factor\":" + String(newFactor, 6) +
                  ",\"calibration_trigger\":false}";
    httpPatch("/feeder_config?id=eq." + configId, body);
  }

  Serial.println("\n╔═══════════════════════════════╗");
  Serial.println("║  CALIBRATION COMPLETE ✔       ║");
  Serial.println("║  Now place bowl and type T    ║");
  Serial.println("╚═══════════════════════════════╝\n");
}

// =============================================================================
// NVS — CALIBRATION LOAD
// =============================================================================
void loadCalibrationFromNVS() {
  prefs.begin(NVS_NAMESPACE, true);
  calDoneOnce = prefs.getBool(NVS_KEY_DONE, false);
  if (calDoneOnce) {
    float f = prefs.getFloat(NVS_KEY_FACTOR, 1.0f);
    long  o = (long)prefs.getLong(NVS_KEY_OFFSET, 0);
    prefs.end();
    scale.set_offset(o); scale.set_scale(f); delay(500);
    Serial.printf("[NVS] Cal loaded — factor=%.6f  offset=%ld\n", f, o);
  } else {
    prefs.end();
    scale.set_offset(0); scale.set_scale(1.0f);
    if (scale.is_ready()) scale.tare(10);
    Serial.println("[NVS] No calibration found.");
  }
}

// =============================================================================
// NVS — CALIBRATION SAVE
// =============================================================================
void saveCalibrationToNVS(float factor, long offset) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat(NVS_KEY_FACTOR, factor);
  prefs.putLong(NVS_KEY_OFFSET, offset);
  prefs.putBool(NVS_KEY_DONE, true);
  prefs.end();
  calDoneOnce = true;
  Serial.printf("[NVS] Cal saved — factor=%.6f  offset=%ld\n", factor, offset);
}

// =============================================================================
// READING FUNCTIONS
// =============================================================================
float readRawTrimmed(int n) {
  if (!scale.is_ready()) return 0;
  float* buf = new float[n];
  for (int i = 0; i < n; i++) {
    unsigned long t = millis();
    while (!scale.is_ready() && millis()-t < 500);
    buf[i] = (float)scale.read();
    delay(HX711_MIN_MS);
  }
  for (int i = 1; i < n; i++) {
    float k = buf[i]; int j = i-1;
    while (j >= 0 && buf[j] > k) { buf[j+1] = buf[j]; j--; }
    buf[j+1] = k;
  }
  int tr = max(1, (int)(n * TRIM_PERCENT));
  float s = 0; int c = 0;
  for (int i = tr; i < n-tr; i++) { s += buf[i]; c++; }
  delete[] buf;
  return (c > 0) ? (s / c) : 0.0f;
}

float takeTrimmedReading(int n) {
  if (!scale.is_ready()) return currentWeight;
  float* buf = new float[n];
  for (int i = 0; i < n; i++) {
    unsigned long t = millis();
    while (!scale.is_ready() && millis()-t < 500);
    buf[i] = scale.get_units(1);
    delay(HX711_MIN_MS);
  }
  for (int i = 1; i < n; i++) {
    float k = buf[i]; int j = i-1;
    while (j >= 0 && buf[j] > k) { buf[j+1] = buf[j]; j--; }
    buf[j+1] = k;
  }
  int tr = max(1, (int)(n * TRIM_PERCENT));
  float s = 0; int c = 0;
  for (int i = tr; i < n-tr; i++) { s += buf[i]; c++; }
  delete[] buf;
  return (c > 0) ? (s / c) : 0.0f;
}

float readStableWeight() {
  float r = takeTrimmedReading(SAMPLES_NORMAL);
  return (r < NOISE_FLOOR_G) ? 0.0f : r;
}

// =============================================================================
// REMOTE TARE
// =============================================================================
void remoteTare() {
  Serial.println("[Tare] Ensure platform is COMPLETELY EMPTY.");
  delay(1000);
  float cf = scale.get_scale();
  scale.set_scale(1.0f); scale.set_offset(0); delay(300);
  long newOffset = (long)readRawTrimmed(30);
  scale.set_offset(newOffset); scale.set_scale(cf); delay(300);
  if (calDoneOnce) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putLong(NVS_KEY_OFFSET, newOffset);
    prefs.end();
  }
  if (configId != "" && WiFi.status() == WL_CONNECTED)
    httpPatch("/feeder_config?id=eq." + configId, "{\"tare_trigger\":false}");
  float after = readStableWeight();
  Serial.printf("[Tare] Done. Reading: %.2fg%s\n", after,
    fabsf(after) > 3.0f ? "  ⚠ not zero — check empty platform" : "");
}

// =============================================================================
// REMOTE CALIBRATE
// =============================================================================
void remoteCalibrate(float knownGrams) {
  if (knownGrams <= 0) return;
  Serial.printf("[RemoteCal] %.1fg — sampling in 3s...\n", knownGrams);
  delay(3000);
  scale.set_scale(1.0f); scale.set_offset(0); delay(300);
  long rz = (long)readRawTrimmed(20); scale.set_offset(rz);
  long rw = (long)readRawTrimmed(SAMPLES_CAL);
  long net = rw - rz;
  if (abs(net) < 1000) {
    Serial.println("[RemoteCal] ERROR: net raw too small.");
    if (configId != "")
      httpPatch("/feeder_config?id=eq." + configId,
                "{\"calibration_trigger\":false}");
    return;
  }
  float nf = fabsf((float)net / knownGrams);
  scale.set_offset(rz); scale.set_scale(nf); delay(500);
  float v = fabsf(takeTrimmedReading(10));
  Serial.printf("[RemoteCal] Factor=%.6f  Verify=%.2fg  Err=%.2f%%\n",
    nf, v, fabsf(v - knownGrams) / knownGrams * 100.0f);
  scale.set_scale(1.0f); scale.set_offset(0); delay(300);
  long fz = (long)readRawTrimmed(20);
  scale.set_offset(fz); scale.set_scale(nf); delay(300);
  saveCalibrationToNVS(nf, fz);
  if (configId != "" && WiFi.status() == WL_CONNECTED) {
    String body = "{\"calibration_factor\":" + String(nf, 6) +
                  ",\"calibration_trigger\":false}";
    httpPatch("/feeder_config?id=eq." + configId, body);
  }
}

// =============================================================================
// HELPERS
// =============================================================================
void stopMotor() {
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_STOP);
  delay(100);
  feederServo.detach();
}

bool checkKillSwitch() {
  return digitalRead(KILL_PIN) == LOW;
}

void reportJam() {
  if (configId == "" || WiFi.status() != WL_CONNECTED) {
    Serial.println("[JAM] Offline — jam flagged locally.");
    return;
  }
  httpPatch("/feeder_config?id=eq." + configId,
            "{\"jam_detected\":true,\"motor_enabled\":false}");
}

// =============================================================================
// HTTP HELPERS
// =============================================================================
String httpGet(String ep) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient h;
  h.begin(String(SB_URL) + ep);
  h.addHeader("apikey", SB_KEY);
  h.addHeader("Authorization", String("Bearer ") + SB_KEY);
  h.setTimeout(15000);
  int code = h.GET();
  String body = (code == 200) ? h.getString() : "";
  if (code != 200)
    Serial.printf("[HTTP] GET %s -> %d\n", ep.c_str(), code);
  h.end();
  return body;
}

String httpPatch(String ep, String body) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient h;
  h.begin(String(SB_URL) + ep);
  h.addHeader("apikey", SB_KEY);
  h.addHeader("Authorization", String("Bearer ") + SB_KEY);
  h.addHeader("Content-Type", "application/json");
  h.addHeader("Prefer", "return=minimal");
  int code = h.sendRequest("PATCH", body);
  h.end();
  return String(code);
}

String httpPost(String ep, String body) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient h;
  h.begin(String(SB_URL) + ep);
  h.addHeader("apikey", SB_KEY);
  h.addHeader("Authorization", String("Bearer ") + SB_KEY);
  h.addHeader("Content-Type", "application/json");
  h.addHeader("Prefer", "return=minimal");
  int code = h.POST(body);
  h.end();
  return String(code);
}
