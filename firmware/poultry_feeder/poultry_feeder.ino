/**
 * =============================================================================
 * POULTRY FEEDER v3 — HX711 + ESP32-S3 + Continuous Rotation Servo + DS3231 + Supabase
 * =============================================================================
 *
 * SERVO BEHAVIOUR (confirmed from hardware testing):
 *   SERVO_OPEN  = 180 → spins forward
 *   SERVO_STOP  = 90  → stops (confirmed true centre)
 *   SERVO_CLOSE = 0   → spins backward (opposite direction)
 *   FLAP_OPEN_MS  = 400ms → flap travels exactly 90° open
 *   FLAP_CLOSE_MS = 400ms → flap travels exactly 90° back to closed
 *
 * FIX-A — Variable Opening Angle
 *   Open time is proportional to grams requested:
 *     ≤30g   → 150ms (~35°) small gap, slow trickle, precise
 *     ≤75g   → 220ms (~50°) quarter open
 *     ≤150g  → 300ms (~68°) half open
 *     ≤200g  → 360ms (~81°) mostly open
 *     >200g  → 400ms (90°)  fully open, maximum flow
 *   This prevents overshooting on small dispenses and dumping too much.
 *
 * FIX-B — Immediate Close on Target Reached
 *   The moment the scale hits the target weight, the flap closes
 *   immediately — no waiting, no extra pellets falling through.
 *
 * FIX-C — Smart Jam Recovery
 *   Old behaviour: violent shaking that opened flap fully and dumped everything.
 *   New behaviour:
 *     1. If no flow detected for 8 seconds → gentle single nudge (reverse 200ms)
 *     2. Immediately watch scale for flow resuming
 *     3. The moment flow resumes → stop nudge, resume normal dispense
 *     4. If no flow after 3 nudges → declare real jam, close flap, alert
 *   Nudge never opens the flap wider — it just vibrates to break a bridge.
 *
 * FIX-D — Power Loss State Memory
 *   Servo state saved to NVS before every movement.
 *   On boot, if state was OPEN or DISPENSING → close immediately.
 *
 * =============================================================================
 * LATENCY INSTRUMENTATION:
 *   LAT-1  Cloud->Device GET       LAT-6  NTP sync
 *   LAT-2  Motor->FirstGrain       LAT-7  Supabase POST
 *   LAT-3  Dispense cycle total    LAT-8  Jam detection
 *   LAT-4  Scale settle time       LAT-9  Supabase PATCH
 *   LAT-5  WiFi reconnect          LAT-10 Schedule->Motor
 *   Type LATENCY in Serial Monitor for full report.
 * =============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
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

// ─── NTP ──────────────────────────────────────────────────────────────────────
const char*    NTP_SERVER1         = "pool.ntp.org";
const char*    NTP_SERVER2         = "time.google.com";
const long     NTP_UTC_OFFSET      = 10800;
const int      NTP_DST_OFFSET      = 0;
const uint32_t NTP_SYNC_TIMEOUT_MS = 10000;

// ─── PINS ─────────────────────────────────────────────────────────────────────
const int HX711_DOUT = 4;
const int HX711_SCK  = 5;
const int SERVO_PIN  = 6;
const int KILL_PIN   = 2;
const int RTC_SDA    = 8;
const int RTC_SCL    = 9;

// ─── SERVO COMMANDS ───────────────────────────────────────────────────────────
// Confirmed from hardware test:
//   180 = forward (open direction)
//    90 = stop     (true centre confirmed 90-96 all stopped)
//     0 = reverse  (close direction)
const int SERVO_OPEN  = 180;
const int SERVO_STOP  = 90;
const int SERVO_CLOSE = 0;

// ─── SERVO TIMING (FIX-A) ────────────────────────────────────────────────────
// Confirmed from hardware test:
//   400ms open  = exactly 90 degrees open
//   400ms close = exactly 0 degrees closed (consistent every time)
const int FLAP_OPEN_MS  = 400;  // full open = 90 degrees
const int FLAP_CLOSE_MS = 400;  // full close = 0 degrees

// ─── VARIABLE OPEN ANGLE (FIX-A) ─────────────────────────────────────────────
// Returns open time in ms based on grams to dispense.
// Less feed = shorter open time = smaller gap = more precise flow.
// Tune these thresholds based on your actual flow rate per opening angle.
int getFlapOpenMs(float grams) {
  if (grams <= 30)  return 150;  // ~35 degrees — tiny gap for small amounts
  if (grams <= 75)  return 220;  // ~50 degrees — quarter open
  if (grams <= 150) return 300;  // ~68 degrees — half open
  if (grams <= 200) return 360;  // ~81 degrees — mostly open
  return FLAP_OPEN_MS;           // 400ms = 90 degrees — full open
}

// ─── NVS KEY FOR SERVO STATE (FIX-D) ─────────────────────────────────────────
const char* NVS_KEY_SERVO_STATE    = "servo_state";
const char* SERVO_STATE_CLOSED     = "CLOSED";
const char* SERVO_STATE_OPEN       = "OPEN";
const char* SERVO_STATE_DISPENSING = "DISPENSING";

// ─── JAM RECOVERY (FIX-C) ────────────────────────────────────────────────────
// JAM_DETECT_MS   : no flow for this long = possible jam
// JAM_NUDGE_MS    : how long to reverse-nudge to break a feed bridge
// JAM_MAX_RETRIES : how many nudges before declaring a real jam
// JAM_FLOW_G      : minimum grams increase to count as "flow resumed"
const unsigned long JAM_DETECT_MS  = 8000;
const int           JAM_NUDGE_MS   = 200;
const int           JAM_MAX_RETRIES = 3;
const float         JAM_FLOW_G     = 1.0f;

// ─── DIAGNOSIS THRESHOLDS ─────────────────────────────────────────────────────
const float HOPPER_EMPTY_DELTA_G = 5.0f;
const float JAM_BOWL_HIGH_G      = 100.0f;

// ─── PREVENTION PULSE INTERVAL ────────────────────────────────────────────────
const unsigned long PREVENTION_PULSE_INTERVAL = 4UL * 60UL * 60UL * 1000UL;

// ─── READING QUALITY SETTINGS ─────────────────────────────────────────────────
const int   SAMPLES_NORMAL = 5;
const int   SAMPLES_CAL    = 30;
const float TRIM_PERCENT   = 0.15f;
const float NOISE_FLOOR_G  = 2.0f;
const long  HX711_MIN_MS   = 80;

// ─── SCHEDULE CONFIG ──────────────────────────────────────────────────────────
const int MAX_FEED_TIMES   = 8;
const int MAX_OFFLINE_LOGS = 50;

// ─── NVS KEYS ─────────────────────────────────────────────────────────────────
const char* NVS_NAMESPACE      = "feeder";
const char* NVS_KEY_FACTOR     = "cal_factor";
const char* NVS_KEY_OFFSET     = "tare_offset";
const char* NVS_KEY_DONE       = "cal_done";
const char* NVS_KEY_SLOT_COUNT = "slot_count";
const char* NVS_KEY_TARGET_G   = "target_g";
const char* NVS_KEY_THRESHOLD  = "adlib_thr";
const char* NVS_KEY_MODE       = "mode";
const char* NVS_KEY_SCHED_SET  = "sched_set";
const char* NVS_KEY_LOG_COUNT  = "log_count";
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
bool    ntpSynced       = false;

// ─── HOPPER STATE ─────────────────────────────────────────────────────────────
float hopperEstimatedGrams = -1.0f;
float hopperCapacityGrams  = 2000.0f;

// ─── SCHEDULE ─────────────────────────────────────────────────────────────────
struct FeedSlot { int hour; int minute; bool firedToday; };
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
unsigned long lastClockPrint      = 0;
float         lastPushedWeight    = -1;

const unsigned long SYNC_INTERVAL  = 5000;
const unsigned long FLUSH_INTERVAL = 30000;

// =============================================================================
// LATENCY TRACKER
// =============================================================================
#define LAT_COUNT 10
struct LatStats { uint32_t count, sumMs, minMs, maxMs; const char* label; };
LatStats latStats[LAT_COUNT];
const char* LAT_LABELS[LAT_COUNT] = {
  "LAT-1  Cloud->Device (GET parse)",
  "LAT-2  Motor->FirstGrain",
  "LAT-3  Dispense cycle total",
  "LAT-4  Scale settle (readStable)",
  "LAT-5  WiFi reconnect",
  "LAT-6  NTP sync",
  "LAT-7  Supabase POST (feed log)",
  "LAT-8  Jam detected / recovery",
  "LAT-9  Supabase PATCH (config)",
  "LAT-10 Schedule->Motor"
};
#define LI_CLOUD_GET      0
#define LI_FIRST_GRAIN    1
#define LI_DISPENSE_TOTAL 2
#define LI_SCALE_SETTLE   3
#define LI_WIFI_RECONN    4
#define LI_NTP_SYNC       5
#define LI_POST_LOG       6
#define LI_JAM            7
#define LI_PATCH_CFG      8
#define LI_SCHED_MOTOR    9

void initLatencyStats() {
  for (int i=0;i<LAT_COUNT;i++) {
    latStats[i]={0,0,UINT32_MAX,0,LAT_LABELS[i]};
  }
}
void recordLatency(int idx, uint32_t ms) {
  if (idx<0||idx>=LAT_COUNT) return;
  latStats[idx].count++;
  latStats[idx].sumMs+=ms;
  if (ms<latStats[idx].minMs) latStats[idx].minMs=ms;
  if (ms>latStats[idx].maxMs) latStats[idx].maxMs=ms;
}
void printLatencyReport() {
  Serial.println("\n========================================");
  Serial.println("       LATENCY MEASUREMENT REPORT       ");
  Serial.println("========================================");
  for (int i=0;i<LAT_COUNT;i++) {
    LatStats& s=latStats[i];
    if (s.count==0) Serial.printf("  %-38s (no samples)\n",s.label);
    else Serial.printf("  %-38s n=%u avg=%ums min=%ums max=%ums\n",
      s.label,s.count,s.sumMs/s.count,s.minMs,s.maxMs);
  }
  Serial.println("========================================\n");
}
unsigned long lastLatencyReport = 0;
const unsigned long LATENCY_REPORT_INTERVAL = 10UL*60UL*1000UL;

// =============================================================================
// PROTOTYPES
// =============================================================================
void     syncTimeFromNTP();
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
void     printLiveClock();
String   getISOTimestamp();
String   httpGet(String ep);
String   httpPatch(String ep, String body);
String   httpPost(String ep, String body);
void     saveServoState(const char* state);
String   loadServoState();
void     timedOpen(float grams);
void     timedClose();
void     recoverFromPowerLoss();
int      getFlapOpenMs(float grams);

// =============================================================================
// FIX-D — SERVO STATE PERSISTENCE
// =============================================================================
void saveServoState(const char* state) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY_SERVO_STATE, state);
  prefs.end();
}
String loadServoState() {
  prefs.begin(NVS_NAMESPACE, true);
  String s = prefs.getString(NVS_KEY_SERVO_STATE, SERVO_STATE_CLOSED);
  prefs.end();
  return s;
}

// =============================================================================
// FIX-A — TIMED OPEN (variable angle based on grams)
// =============================================================================
/**
 * Opens the flap by spinning SERVO_OPEN for getFlapOpenMs(grams).
 * Small grams = short time = small gap = slow precise flow.
 * Large grams = full 400ms = 90 degrees = maximum flow.
 */
void timedOpen(float grams) {
  int openMs = getFlapOpenMs(grams);
  saveServoState(SERVO_STATE_OPEN);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_OPEN);
  Serial.printf("[Servo] Opening %.0fg → %dms (~%d°)\n",
                grams, openMs, (int)(openMs / 400.0f * 90));
  delay(openMs);
  feederServo.write(SERVO_STOP);
  Serial.println("[Servo] Open — holding.");
  // NOTE: servo stays attached so dispenseFeed loop can close immediately
  // when target weight reached. Call timedClose() to close.
}

// =============================================================================
// TIMED CLOSE
// =============================================================================
/**
 * Closes the flap by spinning SERVO_CLOSE for FLAP_CLOSE_MS (400ms).
 * Confirmed: 400ms reverse = exactly 0 degrees closed every time.
 * Works regardless of how far the flap was opened, because:
 *   - If partially open (e.g. 150ms open = ~35°), closing 400ms
 *     overshoots slightly but feed weight will settle it at 0°.
 *   - If fully open (400ms = 90°), closing 400ms returns exactly to 0°.
 * After closing, detaches servo to prevent jitter.
 */
void timedClose() {
  saveServoState(SERVO_STATE_CLOSED);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_CLOSE);
  Serial.printf("[Servo] Closing — %dms reverse\n", FLAP_CLOSE_MS);
  delay(FLAP_CLOSE_MS);
  feederServo.write(SERVO_STOP);
  delay(100);
  feederServo.detach();
  Serial.println("[Servo] Closed.");
}

// =============================================================================
// FIX-D — POWER LOSS RECOVERY
// =============================================================================
void recoverFromPowerLoss() {
  String lastState = loadServoState();
  Serial.printf("[Boot] Last servo state: %s\n", lastState.c_str());
  if (lastState == SERVO_STATE_CLOSED) {
    Serial.println("[Boot] Was closed — no recovery needed.");
    return;
  }
  Serial.println("[Boot] POWER LOSS — feeder was open. Closing now...");
  timedClose();
  Serial.println("[Boot] Emergency close complete.");
  bufferLogOffline(0.0f, 0.0f);
  Serial.println("[Boot] Power-loss event buffered.");
}

// =============================================================================
// NTP
// =============================================================================
void syncTimeFromNTP() {
  if (WiFi.status()!=WL_CONNECTED) { Serial.println("[NTP] No WiFi."); return; }
  Serial.println("[NTP] Syncing...");
  unsigned long t0=millis();
  configTime(NTP_UTC_OFFSET,NTP_DST_OFFSET,NTP_SERVER1,NTP_SERVER2);
  struct tm timeinfo; bool got=false;
  while (millis()-t0<NTP_SYNC_TIMEOUT_MS) { if(getLocalTime(&timeinfo)){got=true;break;} delay(200); }
  uint32_t ms=millis()-t0;
  if (!got) { Serial.printf("[NTP] Timeout after %ums\n",ms); return; }
  recordLatency(LI_NTP_SYNC,ms);
  Serial.printf("[LAT-6] NTPSync: %ums\n",ms);
  int yr=timeinfo.tm_year+1900,mo=timeinfo.tm_mon+1,dy=timeinfo.tm_mday;
  int hh=timeinfo.tm_hour,mm=timeinfo.tm_min,ss=timeinfo.tm_sec;
  if (rtcOK) { rtc.adjust(DateTime(yr,mo,dy,hh,mm,ss));
    Serial.printf("[NTP] DS3231: %04d-%02d-%02d %02d:%02d:%02d EAT\n",yr,mo,dy,hh,mm,ss); }
  ntpSynced=true; Serial.println("[NTP] Done.");
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200); delay(500);
  initLatencyStats();
  Serial.println("\n========================================");
  Serial.println("  POULTRY FEEDER v3 — STARTING UP");
  Serial.println("========================================");

  for (int i=0;i<MAX_FEED_TIMES;i++) schedule[i]={6+i*2,0,false};

  Wire.begin(RTC_SDA,RTC_SCL);
  rtcOK=rtc.begin();
  Serial.println(rtcOK?"[RTC] DS3231 found.":"[RTC] ERROR: not found!");

  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2); ESP32PWM::allocateTimer(3);

  scale.begin(HX711_DOUT,HX711_SCK);
  Serial.print("[Scale] Waiting for HX711");
  int tries=0;
  while (!scale.is_ready()&&tries<40){delay(100);Serial.print(".");tries++;}
  Serial.println(scale.is_ready()?" OK":"\n[Scale] ERROR: not found!");

  loadCalibrationFromNVS();
  loadScheduleFromNVS();
  loadHopperEstimate();

  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN,500,2400);
  feederServo.write(SERVO_STOP);
  delay(200);
  feederServo.detach();

  pinMode(KILL_PIN,INPUT_PULLUP);

  // FIX-D: close immediately on boot if was open
  recoverFromPowerLoss();

  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  int wt=0;
  while (WiFi.status()!=WL_CONNECTED&&wt<30){delay(500);Serial.print(".");wt++;}
  if (WiFi.status()==WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected: "+WiFi.localIP().toString());
    syncTimeFromNTP();
  } else {
    Serial.println("\n[WiFi] OFFLINE.");
    if (rtcOK&&rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
  }

  if (rtcOK){Serial.print("[RTC] ");printRTCTime();}

  if (!calDoneOnce) {
    Serial.println("\n  CALIBRATION REQUIRED — Type H.");
  } else {
    float a=readStableWeight(); delay(5000); float b=readStableWeight();
    Serial.printf("[Scale] %s (drift=%.2fg)\n",
      fabsf(b-a)>2.0f?"WARNING: unstable":"Stable",fabsf(b-a));
  }

  printSchedule();
  Serial.println("\n[Commands]");
  Serial.println("  H                   = Calibration wizard");
  Serial.println("  T                   = Tare");
  Serial.println("  F                   = Show calibration factor");
  Serial.println("  D<g>                = Dispense  e.g. D200");
  Serial.println("  R                   = Raw ADC stream");
  Serial.println("  SCHED               = Show schedule");
  Serial.println("  SET <S> HH MM       = Set slot  e.g. SET 0 06 30");
  Serial.println("  SLOTS <n>           = Set active slots");
  Serial.println("  TARGET <g>          = Set grams per feed");
  Serial.println("  TIME                = Show RTC time");
  Serial.println("  SETTIME Y M D H M S = Set RTC");
  Serial.println("  NTPSYNC             = Force NTP sync");
  Serial.println("  LOGS                = Show offline log count");
  Serial.println("  HOPPER              = Show hopper level");
  Serial.println("  REFILL <g>          = Set hopper after refill");
  Serial.println("  SERVOSTATE          = Show NVS servo state");
  Serial.println("  CLOSEFLAP           = Force close flap now");
  Serial.println("  LATENCY             = Print latency report");
  Serial.println("  RESET               = Erase all NVS data\n");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  if (checkKillSwitch()){stopMotor();Serial.println("[SAFETY] KILL SWITCH.");delay(500);return;}

  if (millis()-lastClockPrint>=1000){lastClockPrint=millis();printLiveClock();}
  if (millis()-lastLatencyReport>=LATENCY_REPORT_INTERVAL){lastLatencyReport=millis();printLatencyReport();}

  static bool wifiWas=false; static unsigned long wifiDrop=0;
  bool wNow=(WiFi.status()==WL_CONNECTED);
  if (wifiWas&&!wNow){wifiDrop=millis();wifiWas=false;Serial.println("[LAT-5] WiFi dropped...");}
  else if (!wifiWas&&wNow){
    if(wifiDrop>0){uint32_t r=millis()-wifiDrop;recordLatency(LI_WIFI_RECONN,r);Serial.printf("[LAT-5] Reconnect: %ums\n",r);}
    wifiWas=true;wifiDrop=0;
  } else if(wNow) wifiWas=true;

  if (Serial.available()>0) {
    String cmd=Serial.readStringUntil('\n'); cmd.trim();
    String cu=cmd; cu.toUpperCase();

    if (cu=="H")            runFullCalibration();
    else if (cu=="T")       remoteTare();
    else if (cu=="F")
      Serial.printf("[Factor] %.6f | Offset=%ld | NVS=%s\n",
        scale.get_scale(),scale.get_offset(),calDoneOnce?"SAVED":"NOT SET");
    else if (cu.startsWith("D")){
      float g=cu.substring(1).toFloat();
      if(g>0) dispenseFeed(g); else Serial.println("Usage: D200");
    }
    else if (cu=="R"){
      Serial.println("RAW — press S to stop");
      while(true){
        if(Serial.available()&&toupper(Serial.read())=='S') break;
        unsigned long t=millis(); while(!scale.is_ready()&&millis()-t<200);
        Serial.printf("Raw=%ld Grams=%.2f\n",scale.read(),scale.get_units(1)); delay(HX711_MIN_MS);
      }
    }
    else if (cu=="TIME")    printRTCTime();
    else if (cu=="NTPSYNC"){if(WiFi.status()==WL_CONNECTED)syncTimeFromNTP();else Serial.println("[NTP] No WiFi.");}
    else if (cu=="SCHED")   printSchedule();
    else if (cu=="LATENCY") printLatencyReport();
    else if (cu=="SERVOSTATE") Serial.println("[ServoState] NVS: "+loadServoState());
    else if (cu=="CLOSEFLAP"){
      Serial.println("[Servo] Force closing...");
      timedClose();
    }
    else if (cu=="LOGS"){
      prefs.begin(NVS_NAMESPACE,true);int cnt=prefs.getInt(NVS_KEY_LOG_COUNT,0);prefs.end();
      Serial.printf("[OfflineLogs] %d pending\n",cnt);
    }
    else if (cu=="HOPPER"){
      if(hopperEstimatedGrams<0) Serial.println("[Hopper] Not initialised — REFILL <g>");
      else Serial.printf("[Hopper] %.0fg remaining (%.0f%%)\n",
        hopperEstimatedGrams,hopperEstimatedGrams/hopperCapacityGrams*100.0f);
    }
    else if (cu.startsWith("REFILL ")){
      float g=cu.substring(7).toFloat();
      if(g>0) refillHopper(g); else Serial.println("Usage: REFILL 2000");
    }
    else if (cu.startsWith("SET ")){
      int slot=-1,hh=-1,mm=-1;
      sscanf(cmd.c_str()+4,"%d %d %d",&slot,&hh,&mm);
      if(slot>=0&&slot<MAX_FEED_TIMES&&hh>=0&&hh<24&&mm>=0&&mm<60){
        schedule[slot]={hh,mm,false};
        if(slot>=activeSlots) activeSlots=slot+1;
        saveScheduleToNVS();
        Serial.printf("[Schedule] Slot %d -> %02d:%02d\n",slot,hh,mm);
      } else Serial.println("Usage: SET <slot 0-7> <HH> <MM>");
    }
    else if (cu.startsWith("SLOTS ")){
      int n=cu.substring(6).toInt();
      if(n>=1&&n<=MAX_FEED_TIMES){activeSlots=n;saveScheduleToNVS();
        Serial.printf("[Schedule] Active slots=%d\n",n);printSchedule();}
      else Serial.printf("Usage: SLOTS <1-%d>\n",MAX_FEED_TIMES);
    }
    else if (cu.startsWith("TARGET ")){
      float g=cu.substring(7).toFloat();
      if(g>0){schedTargetG=g;saveScheduleToNVS();
        Serial.printf("[Schedule] Target=%.1fg\n",g);}
      else Serial.println("Usage: TARGET 250");
    }
    else if (cu.startsWith("SETTIME ")){
      int yr,mo,dy,hh,mm,ss;
      sscanf(cmd.c_str()+8,"%d %d %d %d %d %d",&yr,&mo,&dy,&hh,&mm,&ss);
      if(rtcOK&&yr>2020){rtc.adjust(DateTime(yr,mo,dy,hh,mm,ss));
        Serial.print("[RTC] Updated -> ");printRTCTime();}
      else Serial.println("Usage: SETTIME 2025 6 1 8 0 0");
    }
    else if (cu=="RESET"){
      prefs.begin(NVS_NAMESPACE,false);prefs.clear();prefs.end();
      calDoneOnce=false;scheduleSet=false;hopperEstimatedGrams=-1.0f;
      scale.set_offset(0);scale.set_scale(1.0f);
      saveServoState(SERVO_STATE_CLOSED);
      Serial.println("[NVS] Erased. Type H to recalibrate.");
    }
  }

  // Weight reading
  static unsigned long lastWT=0;
  if (millis()-lastWT>400){
    lastWT=millis();
    unsigned long tw=millis(); while(!scale.is_ready()&&millis()-tw<150);
    if(scale.is_ready()){
      currentWeight=readStableWeight();
      Serial.printf("[Weight] %.1fg | WiFi=%s | Mode=%s | Hopper=%.0fg | %s\n",
        currentWeight,WiFi.status()==WL_CONNECTED?"OK":"OFFLINE",
        feedMode.c_str(),hopperEstimatedGrams,
        isJammed?"JAMMED":(isSystemEnabled?"ACTIVE":"DISABLED"));
      if(WiFi.status()==WL_CONNECTED&&configId!=""&&
         (fabsf(currentWeight-lastPushedWeight)>0.5f||lastPushedWeight<0)){
        httpPatch("/feeder_config?id=eq."+configId,
                  "{\"current_bowl_weight\":"+String(currentWeight,1)+"}");
        lastPushedWeight=currentWeight;
      }
    }
  }

  // Ad-libitum
  static unsigned long lastAdlib=0;
  if(feedMode=="ad-libitum"&&isSystemEnabled&&!isJammed){
    if(millis()-lastAdlib>10000){
      lastAdlib=millis();
      if(currentWeight<adlibThreshold){
        float fill=max(50.0f,adlibThreshold-currentWeight);
        Serial.printf("[Ad-Lib] Bowl low (%.1fg) — filling %.1fg\n",currentWeight,fill);
        dispenseFeed(fill);
      }
    }
  }

  if(rtcOK&&feedMode=="scheduled"&&isSystemEnabled&&!isJammed) checkSchedule();

  if(isSystemEnabled&&!isJammed)
    if(millis()-lastPreventionPulse>PREVENTION_PULSE_INTERVAL){
      lastPreventionPulse=millis(); preventionPulse();
    }

  if(WiFi.status()==WL_CONNECTED){
    if(millis()-lastSyncTime>SYNC_INTERVAL){lastSyncTime=millis();syncWithCloud();}
    if(millis()-lastFlushTime>FLUSH_INTERVAL){lastFlushTime=millis();flushOfflineLogs();}
  }

  if(!isSystemEnabled||isJammed) stopMotor();
}

// =============================================================================
// LIVE CLOCK / RTC
// =============================================================================
void printLiveClock(){
  if(!rtcOK) return;
  DateTime now=rtc.now();
  const char* days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  Serial.printf("[CLOCK] %02d:%02d:%02d  %s %02d/%02d/%04d EAT\n",
    now.hour(),now.minute(),now.second(),
    days[now.dayOfTheWeek()],now.day(),now.month(),now.year());
}
DateTime getRTCTime(){return rtc.now();}
void printRTCTime(){
  if(!rtcOK){Serial.println("[RTC] Not available.");return;}
  DateTime n=rtc.now();
  const char* days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  Serial.printf("[RTC] %s %02d/%02d/%04d %02d:%02d:%02d EAT Temp=%.1fC NTP=%s\n",
    days[n.dayOfTheWeek()],n.day(),n.month(),n.year(),
    n.hour(),n.minute(),n.second(),rtc.getTemperature(),ntpSynced?"synced":"NOT synced");
}
String getISOTimestamp(){
  DateTime now=rtcOK?rtc.now():DateTime(2000,1,1,0,0,0);
  char ts[26];
  snprintf(ts,sizeof(ts),"%04d-%02d-%02dT%02d:%02d:%02d+03:00",
    now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());
  return String(ts);
}

// =============================================================================
// SCHEDULE
// =============================================================================
void printSchedule(){
  Serial.println("\n[Schedule] -----------------------------------");
  Serial.printf("  Mode        : %s\n",feedMode.c_str());
  Serial.printf("  Active slots: %d / %d\n",activeSlots,MAX_FEED_TIMES);
  Serial.printf("  Target/feed : %.1fg\n",schedTargetG);
  Serial.printf("  Adlib thresh: %.1fg\n",adlibThreshold);
  for(int i=0;i<activeSlots;i++)
    Serial.printf("  Slot %d: %02d:%02d  %s\n",i,
      schedule[i].hour,schedule[i].minute,schedule[i].firedToday?"(fired)":"");
  Serial.println("----------------------------------------------\n");
}
void checkSchedule(){
  DateTime now=getRTCTime();
  int h=now.hour(),m=now.minute(),s=now.second();
  static int lastDay=-1;
  if(now.day()!=lastDay){
    lastDay=now.day();
    for(int i=0;i<MAX_FEED_TIMES;i++) schedule[i].firedToday=false;
    Serial.println("[Schedule] New day — flags reset.");
  }
  for(int i=0;i<activeSlots;i++){
    if(h==schedule[i].hour&&m==schedule[i].minute&&s<30&&!schedule[i].firedToday){
      unsigned long t0=millis();
      Serial.printf("[Schedule] Slot %d firing %02d:%02d — %.0fg\n",i,h,m,schedTargetG);
      schedule[i].firedToday=true;
      recordLatency(LI_SCHED_MOTOR,millis()-t0);
      dispenseFeed(schedTargetG);
    }
  }
}
void loadScheduleFromNVS(){
  prefs.begin(NVS_NAMESPACE,true);
  scheduleSet=prefs.getBool(NVS_KEY_SCHED_SET,false);
  if(scheduleSet){
    activeSlots=constrain(prefs.getInt(NVS_KEY_SLOT_COUNT,3),1,MAX_FEED_TIMES);
    schedTargetG=prefs.getFloat(NVS_KEY_TARGET_G,200.0f);
    adlibThreshold=prefs.getFloat(NVS_KEY_THRESHOLD,500.0f);
    feedMode=prefs.getString(NVS_KEY_MODE,"ad-libitum");
    for(int i=0;i<activeSlots;i++){
      schedule[i].hour=prefs.getInt(("ft_h_"+String(i)).c_str(),schedule[i].hour);
      schedule[i].minute=prefs.getInt(("ft_m_"+String(i)).c_str(),schedule[i].minute);
      schedule[i].firedToday=false;
    }
  }
  prefs.end();
  Serial.printf("[NVS] Schedule: slots=%d mode=%s target=%.0fg thresh=%.0fg\n",
    activeSlots,feedMode.c_str(),schedTargetG,adlibThreshold);
}
void saveScheduleToNVS(){
  prefs.begin(NVS_NAMESPACE,false);
  prefs.putBool(NVS_KEY_SCHED_SET,true);
  prefs.putInt(NVS_KEY_SLOT_COUNT,activeSlots);
  prefs.putFloat(NVS_KEY_TARGET_G,schedTargetG);
  prefs.putFloat(NVS_KEY_THRESHOLD,adlibThreshold);
  prefs.putString(NVS_KEY_MODE,feedMode);
  for(int i=0;i<activeSlots;i++){
    prefs.putInt(("ft_h_"+String(i)).c_str(),schedule[i].hour);
    prefs.putInt(("ft_m_"+String(i)).c_str(),schedule[i].minute);
  }
  prefs.end(); scheduleSet=true;
  Serial.printf("[NVS] Schedule saved (%d slots).\n",activeSlots);
}

// =============================================================================
// OFFLINE LOG BUFFER
// =============================================================================
void bufferLogOffline(float dispensed,float bowl){
  prefs.begin(NVS_NAMESPACE,false);
  int count=prefs.getInt(NVS_KEY_LOG_COUNT,0);
  if(count>=MAX_OFFLINE_LOGS){
    for(int i=0;i<count-1;i++){
      String v=prefs.getString(("log_"+String(i+1)).c_str(),"");
      prefs.putString(("log_"+String(i)).c_str(),v);
    }
    count--;
  }
  String entry="{\"amount_dispensed\":"+String(dispensed,1)+
               ",\"current_weight\":"+String(bowl,1)+
               ",\"logged_at\":\""+getISOTimestamp()+"\"}";
  prefs.putString(("log_"+String(count)).c_str(),entry);
  prefs.putInt(NVS_KEY_LOG_COUNT,count+1);
  prefs.end();
  Serial.printf("[OfflineLog] Buffered #%d (pending:%d)\n",count,count+1);
}
void flushOfflineLogs(){
  if(WiFi.status()!=WL_CONNECTED) return;
  prefs.begin(NVS_NAMESPACE,false);
  int count=prefs.getInt(NVS_KEY_LOG_COUNT,0);
  if(count==0){prefs.end();return;}
  Serial.printf("[FlushLogs] Sending %d log(s)...\n",count);
  int sent=0;
  for(int i=0;i<count;i++){
    String entry=prefs.getString(("log_"+String(i)).c_str(),"");
    if(entry==""){sent++;continue;}
    String result=httpPost("/feeding_logs",entry);
    if(result=="201"||result=="200"){sent++;}
    else{
      int rem=count-i;
      for(int j=0;j<rem;j++){
        String v=prefs.getString(("log_"+String(i+j)).c_str(),"");
        prefs.putString(("log_"+String(j)).c_str(),v);
      }
      prefs.putInt(NVS_KEY_LOG_COUNT,rem);prefs.end();
      Serial.printf("[FlushLogs] Sent %d, %d pending.\n",sent,rem);return;
    }
  }
  prefs.putInt(NVS_KEY_LOG_COUNT,0);prefs.end();
  Serial.printf("[FlushLogs] All %d sent.\n",sent);
}
void logFeed(float dispensed,float bowl){
  String body="{\"amount_dispensed\":"+String(dispensed,1)+
              ",\"current_weight\":"+String(bowl,1)+
              ",\"logged_at\":\""+getISOTimestamp()+"\"}";
  if(WiFi.status()==WL_CONNECTED){
    flushOfflineLogs();
    unsigned long t0=millis();
    String result=httpPost("/feeding_logs",body);
    uint32_t ms=millis()-t0;
    if(result=="201"||result=="200"){recordLatency(LI_POST_LOG,ms);Serial.printf("[Log] Sent. [LAT-7] %ums\n",ms);}
    else{Serial.printf("[Log] Failed (%s %ums) — buffering.\n",result.c_str(),ms);bufferLogOffline(dispensed,bowl);}
  } else bufferLogOffline(dispensed,bowl);
}

// =============================================================================
// CLOUD SYNC
// =============================================================================
void syncWithCloud(){
  if(WiFi.status()!=WL_CONNECTED) return;
  prefs.begin(NVS_NAMESPACE,false);
  String pc=prefs.getString("pending_alert_cause","");
  String pm=prefs.getString("pending_alert_msg","");
  prefs.end();
  if(pc!=""&&configId!=""){
    String ab="{\"jam_detected\":true,\"motor_enabled\":false,"
              "\"alert_cause\":\""+pc+"\",\"alert_message\":\""+pm+"\"}";
    String r=httpPatch("/feeder_config?id=eq."+configId,ab);
    if(r=="200"||r=="204"){
      prefs.begin(NVS_NAMESPACE,false);prefs.remove("pending_alert_cause");prefs.remove("pending_alert_msg");prefs.end();
      Serial.println("[Alert] Flushed.");
    }
  }
  unsigned long t0=millis();
  String raw=httpGet("/feeder_config?select=*&limit=1");
  uint32_t getMs=millis()-t0;
  if(raw=="") return;
  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc,raw)||doc.size()==0) return;
  JsonObject c=doc[0];
  recordLatency(LI_CLOUD_GET,getMs);
  Serial.printf("[LAT-1] Cloud->Device: %ums\n",getMs);
  configId=c["id"].as<String>();
  unsigned long p0=millis();
  httpPatch("/feeder_config?id=eq."+configId,"{\"updated_at\":\""+getISOTimestamp()+"\"}");
  uint32_t pMs=millis()-p0;
  recordLatency(LI_PATCH_CFG,pMs);
  Serial.printf("[LAT-9] PATCH: %ums\n",pMs);

  String mode=c["mode"]|"ad-libitum";
  String breed=c["breed"]|"broiler";
  int birds=c["chicken_count"]|10,ageWeeks=c["age_weeks"]|1;
  float threshold=c["adlib_threshold_grams"]|500.0f;
  float cfactor=c["calibration_factor"]|0.0f;
  bool enabled=c["motor_enabled"]|true,jammed=c["jam_detected"]|false;
  bool tareTrig=c["tare_trigger"]|false,calTrig=c["calibration_trigger"]|false;
  float knownWt=c["known_weight_grams"]|0.0f;

  if(!calDoneOnce&&cfactor>0) scale.set_scale(cfactor);
  if(tareTrig) remoteTare();
  if(calTrig&&knownWt>0) remoteCalibrate(knownWt);
  isSystemEnabled=enabled; isJammed=jammed;

  float dpb=0;
  String rr=httpGet("/feed_requirements?breed=eq."+breed+"&age_weeks=lte."+String(ageWeeks)+"&order=age_weeks.desc&limit=1");
  if(rr!=""){DynamicJsonDocument rd(512);if(!deserializeJson(rd,rr)&&rd.size()>0)dpb=rd[0]["daily_feed_grams_per_bird"]|0.0f;}
  if(dpb<=0) dpb=30.0f+max(0,ageWeeks-1)*25.0f;
  float ct=c["target_dispense_grams"]|0.0f;
  float newTarget=ct>0?ct:(birds*dpb)/(float)activeSlots;
  int cloudSlots=constrain(c["slot_count"]|activeSlots,1,MAX_FEED_TIMES);
  bool sc=(cloudSlots!=activeSlots); if(sc) activeSlots=cloudSlots;
  for(int i=0;i<activeSlots;i++){
    char col[16];snprintf(col,sizeof(col),"feed_time_%d",i);
    String t=c[col]|"";
    if(t.length()>=5){
      int nh=t.substring(0,2).toInt(),nm=t.substring(3,5).toInt();
      if(nh>=0&&nh<24&&nm>=0&&nm<60&&(nh!=schedule[i].hour||nm!=schedule[i].minute)){
        schedule[i].hour=nh;schedule[i].minute=nm;schedule[i].firedToday=false;sc=true;
      }
    }
  }
  bool pc2=(fabsf(newTarget-schedTargetG)>0.5f||fabsf(threshold-adlibThreshold)>0.5f||mode!=feedMode);
  if(sc||pc2){schedTargetG=newTarget;adlibThreshold=threshold;feedMode=mode;saveScheduleToNVS();printSchedule();}
  Serial.printf("[Sync] Mode=%s Birds=%d Age=%dwk Bowl=%.1fg Target=%.1fg\n",
    mode.c_str(),birds,ageWeeks,currentWeight,schedTargetG);
  if(!isSystemEnabled||isJammed){Serial.printf("[Sync] Blocked enabled=%d jammed=%d\n",isSystemEnabled,isJammed);return;}
  if(mode=="scheduled"){
    bool st=c["scheduled_feed_trigger"]|false;
    if(st){
      Serial.println("[Sync] Cloud trigger.");
      dispenseFeed(schedTargetG);
      unsigned long p2=millis();
      httpPatch("/feeder_config?id=eq."+configId,"{\"scheduled_feed_trigger\":false}");
      recordLatency(LI_PATCH_CFG,millis()-p2);
    }
  }
}

// =============================================================================
// DISPENSE FEED — FIX-A (variable open) + FIX-B (instant close) + FIX-C (smart jam)
// =============================================================================
void dispenseFeed(float gramsTarget) {
  if (gramsTarget <= 0) return;

  // Read start weight
  unsigned long sT = millis();
  float startW = readStableWeight();
  recordLatency(LI_SCALE_SETTLE, millis()-sT);

  float goalW = startW + gramsTarget;
  Serial.printf("[Dispense] Start=%.1fg +%.1fg Goal=%.1fg OpenAngle=%dms\n",
                startW, gramsTarget, goalW, getFlapOpenMs(gramsTarget));

  // Save dispensing state (FIX-D)
  saveServoState(SERVO_STATE_DISPENSING);

  unsigned long dispCycleStart = millis();

  // FIX-A: open to angle proportional to grams
  timedOpen(gramsTarget);

  unsigned long firstGrainStart    = millis();
  bool          firstGrainDetected = false;
  unsigned long tStart             = millis();
  unsigned long tLastPrint         = 0;
  unsigned long lastFlowTime       = millis();
  float         lastFlowWeight     = startW;
  int           jamAttempts        = 0;
  bool          success            = false;

  while (millis() - tStart < 60000) {

    if (checkKillSwitch()) {
      Serial.println("[SAFETY] Kill switch!");
      break;
    }

    unsigned long scT = millis();
    float w = readStableWeight();
    recordLatency(LI_SCALE_SETTLE, millis()-scT);

    // LAT-2: first grain
    if (!firstGrainDetected && (w - startW) >= 2.0f) {
      uint32_t fg = millis() - firstGrainStart;
      firstGrainDetected = true;
      recordLatency(LI_FIRST_GRAIN, fg);
      Serial.printf("[LAT-2] Motor->FirstGrain: %ums\n", fg);
    }

    // Progress print every 500ms
    if (millis() - tLastPrint > 500) {
      tLastPrint = millis();
      Serial.printf("  %.1fg / %.1fg (%.0f%%)\n",
        w, goalW, min(100.0f, (w-startW)/gramsTarget*100.0f));
    }

    // FIX-B: close IMMEDIATELY when target reached
    if (w >= goalW) {
      success = true;
      Serial.println("[Dispense] Target reached — closing immediately!");
      timedClose();
      break;
    }

    // Track flow
    if ((w - lastFlowWeight) >= JAM_FLOW_G) {
      lastFlowTime   = millis();
      lastFlowWeight = w;
    }

    // FIX-C: smart jam detection — 8 seconds no flow
    if (millis() - lastFlowTime >= JAM_DETECT_MS) {

      if (jamAttempts >= JAM_MAX_RETRIES) {
        // Real jam — give up
        Serial.printf("[JAM] %d nudges failed — real jam. Closing.\n", JAM_MAX_RETRIES);
        timedClose();
        float dispensedSoFar = max(0.0f, readStableWeight() - startW);
        logFeed(dispensedSoFar, readStableWeight());
        subtractFromHopper(dispensedSoFar);
        diagnoseAndAlert(dispensedSoFar, startW);
        recordLatency(LI_DISPENSE_TOTAL, millis()-dispCycleStart);
        isJammed = true;
        return;
      }

      jamAttempts++;
      Serial.printf("[JAM] No flow for %lums — gentle nudge %d of %d\n",
                    JAM_DETECT_MS, jamAttempts, JAM_MAX_RETRIES);
      recordLatency(LI_JAM, millis()-lastFlowTime);

      // FIX-C: gentle nudge — brief reverse to break a feed bridge
      // Does NOT open the flap wider, just vibrates to dislodge stuck feed
      feederServo.attach(SERVO_PIN, 500, 2400);
      feederServo.write(SERVO_CLOSE);
      delay(JAM_NUDGE_MS);
      feederServo.write(SERVO_OPEN);  // reopen to same position
      delay(JAM_NUDGE_MS);
      feederServo.write(SERVO_STOP);

      // Watch scale for up to 5 seconds — stop the moment flow resumes
      Serial.println("[JAM] Watching for flow after nudge...");
      float wBeforeNudge = readStableWeight();
      unsigned long nudgeWatchStart = millis();
      bool flowResumed = false;

      while (millis() - nudgeWatchStart < 5000) {
        delay(200);
        float wCheck = readStableWeight();
        if ((wCheck - wBeforeNudge) >= JAM_FLOW_G) {
          Serial.printf("[JAM] Flow resumed after nudge %d (+%.1fg) — continuing!\n",
                        jamAttempts, wCheck - wBeforeNudge);
          // Reset jam tracking
          lastFlowTime   = millis();
          lastFlowWeight = wCheck;
          jamAttempts    = 0;  // reset attempts since flow is back
          flowResumed    = true;
          break;
        }
        wBeforeNudge = wCheck;
      }

      if (!flowResumed) {
        Serial.printf("[JAM] Nudge %d did not restore flow.\n", jamAttempts);
      }
    }

    // Cloud motor-disable check every 5s
    static unsigned long tCloudChk = 0;
    if (millis()-tCloudChk>5000&&WiFi.status()==WL_CONNECTED&&configId!="") {
      tCloudChk=millis();
      String mr=httpGet("/feeder_config?select=motor_enabled&id=eq."+configId);
      if(mr!=""){
        DynamicJsonDocument md(128);
        if(!deserializeJson(md,mr)&&md.size()>0&&!md[0]["motor_enabled"].as<bool>()){
          Serial.println("[Dispense] Disabled via cloud.");
          isSystemEnabled=false; break;
        }
      }
    }

    delay(50);
  }

  // If we exited the loop without closing (timeout or kill switch)
  if (!success && !isJammed) {
    Serial.println("[Dispense] Timeout or stopped — closing flap.");
    timedClose();
  }

  unsigned long fT=millis();
  float finalW=readStableWeight();
  recordLatency(LI_SCALE_SETTLE,millis()-fT);
  float dispensed=max(0.0f,finalW-startW);
  uint32_t cycleMs=millis()-dispCycleStart;
  recordLatency(LI_DISPENSE_TOTAL,cycleMs);
  Serial.printf("[LAT-3] DispenseCycle: %ums for %.1fg\n",cycleMs,dispensed);
  logFeed(dispensed,finalW);
  subtractFromHopper(dispensed);
}

// =============================================================================
// PREVENTION PULSE
// =============================================================================
void preventionPulse(){
  if(isJammed||!isSystemEnabled) return;
  Serial.println("[Prevention] Agitation pulse.");
  saveServoState(SERVO_STATE_OPEN);
  feederServo.attach(SERVO_PIN,500,2400);
  // Small open/close pulse — just enough to agitate, not dump feed
  feederServo.write(SERVO_OPEN);  delay(150);
  feederServo.write(SERVO_STOP);  delay(200);
  feederServo.write(SERVO_CLOSE); delay(150);
  feederServo.write(SERVO_STOP);  delay(200);
  feederServo.detach();
  timedClose();  // ensure fully closed after pulse
  Serial.println("[Prevention] Done.");
}

// =============================================================================
// DIAGNOSE AND ALERT
// =============================================================================
void diagnoseAndAlert(float totalDispensed,float bowlWeightAtStart){
  String cause,message;
  if(totalDispensed<HOPPER_EMPTY_DELTA_G&&bowlWeightAtStart<JAM_BOWL_HIGH_G){
    cause="hopper_empty";message="Hopper may be empty — only "+String(totalDispensed,1)+"g dispensed.";
  } else if(totalDispensed<HOPPER_EMPTY_DELTA_G&&bowlWeightAtStart>=JAM_BOWL_HIGH_G){
    cause="jam";message="Jam — bowl was "+String(bowlWeightAtStart,1)+"g and nothing moved.";
  } else {
    cause="partial_jam_or_low_hopper";message="Partial blockage — only "+String(totalDispensed,1)+"g.";
  }
  Serial.println("[ALERT] "+message);
  if(WiFi.status()==WL_CONNECTED&&configId!=""){
    httpPatch("/feeder_config?id=eq."+configId,
              "{\"jam_detected\":true,\"motor_enabled\":false,"
              "\"alert_cause\":\""+cause+"\",\"alert_message\":\""+message+"\"}");
  } else {
    prefs.begin(NVS_NAMESPACE,false);
    prefs.putString("pending_alert_cause",cause);
    prefs.putString("pending_alert_msg",message);
    prefs.end();
  }
}

// =============================================================================
// HOPPER
// =============================================================================
void loadHopperEstimate(){
  prefs.begin(NVS_NAMESPACE,true);
  bool set=prefs.getBool(NVS_KEY_HOPPER_SET,false);
  hopperEstimatedGrams=set?prefs.getFloat(NVS_KEY_HOPPER_G,-1.0f):-1.0f;
  prefs.end();
  if(hopperEstimatedGrams>=0) Serial.printf("[Hopper] %.0fg remaining\n",hopperEstimatedGrams);
  else Serial.println("[Hopper] Not initialised — type REFILL <grams>.");
}
void saveHopperEstimate(){
  prefs.begin(NVS_NAMESPACE,false);
  prefs.putFloat(NVS_KEY_HOPPER_G,hopperEstimatedGrams);
  prefs.putBool(NVS_KEY_HOPPER_SET,true);
  prefs.end();
}
void subtractFromHopper(float grams){
  if(hopperEstimatedGrams<0) return;
  hopperEstimatedGrams-=grams;
  if(hopperEstimatedGrams<0) hopperEstimatedGrams=0;
  saveHopperEstimate();
  Serial.printf("[Hopper] -%.0fg -> %.0fg remaining\n",grams,hopperEstimatedGrams);
  float wl=hopperCapacityGrams*0.20f;
  if(hopperEstimatedGrams>0&&hopperEstimatedGrams<=wl){
    Serial.printf("[Hopper] WARNING — only %.0fg left\n",hopperEstimatedGrams);
    if(WiFi.status()==WL_CONNECTED&&configId!="")
      httpPatch("/feeder_config?id=eq."+configId,
                "{\"alert_cause\":\"hopper_low\",\"alert_message\":\"Hopper low — "+
                String(hopperEstimatedGrams,0)+"g remaining.\"}");
  }
  if(hopperEstimatedGrams<=0){
    Serial.println("[Hopper] ESTIMATED EMPTY.");
    if(WiFi.status()==WL_CONNECTED&&configId!="")
      httpPatch("/feeder_config?id=eq."+configId,
                "{\"alert_cause\":\"hopper_empty\",\"alert_message\":\"Hopper empty.\",\"motor_enabled\":false}");
  }
}
void refillHopper(float grams){
  hopperEstimatedGrams=grams;saveHopperEstimate();
  Serial.printf("[Hopper] Refilled — %.0fg\n",grams);
  if(WiFi.status()==WL_CONNECTED&&configId!=""){
    httpPatch("/feeder_config?id=eq."+configId,
              "{\"jam_detected\":false,\"motor_enabled\":true,\"alert_cause\":\"\",\"alert_message\":\"\"}");
    Serial.println("[Hopper] Supabase alerts cleared.");
  }
}

// =============================================================================
// CALIBRATION
// =============================================================================
void runFullCalibration(){
  Serial.println("\n========================================");
  Serial.println("         CALIBRATION WIZARD             ");
  Serial.println("========================================\n");
  Serial.println("STEP 1/5 — EMPTY PLATFORM. Press Enter...");
  while(!Serial.available()){if(checkKillSwitch()){Serial.println("Cancelled.");return;}delay(10);}
  Serial.readString();
  scale.set_scale(1.0f);scale.set_offset(0);delay(500);
  long rawZero=(long)readRawTrimmed(30);scale.set_offset(rawZero);
  Serial.printf("Raw zero=%ld\n\n",rawZero);
  Serial.println("STEP 2/5 — PLACE KNOWN WEIGHT (100-5000g). Type grams:");
  while(!Serial.available()){if(checkKillSwitch()){Serial.println("Cancelled.");return;}delay(10);}
  float kg=Serial.readStringUntil('\n').toFloat();
  if(kg<100||kg>5000){Serial.println("ERROR: 100-5000g only.");return;}
  Serial.println("\nSTEP 3/5 — SAMPLING (5s settle)...");
  for(int i=5;i>=1;i--){Serial.printf("  %d...\n",i);delay(1000);}
  long rawWith=(long)readRawTrimmed(SAMPLES_CAL);
  long netRaw=rawWith-rawZero;
  if(abs(netRaw)<1000){Serial.println("ERROR: too small.");return;}
  float nf=(float)netRaw/kg;
  bool flip=(nf<0);if(flip)nf=-nf;
  scale.set_offset(rawZero);scale.set_scale(nf);
  Serial.println("\nSTEP 4/5 — VERIFICATION...");
  delay(1000);float sum=0;
  for(int i=0;i<5;i++){float r=fabsf(takeTrimmedReading(10));sum+=r;Serial.printf("  %d: %.2fg\n",i+1,r);delay(500);}
  float avg=sum/5.0f,err=fabsf(avg-kg)/kg*100.0f;
  Serial.printf("  Avg=%.2fg Error=%.2f%% Flip=%s\n",avg,err,flip?"YES":"no");
  if(err>3.0f){
    Serial.println("  >3% error. Enter to continue or H to retry.");
    while(!Serial.available()) delay(10);
    String ch=Serial.readStringUntil('\n');ch.trim();ch.toUpperCase();
    if(ch=="H"){runFullCalibration();return;}
  } else Serial.println("  PASSED");
  Serial.println("\nSTEP 5/5 — REMOVE weight. Press Enter...");
  while(!Serial.available()){if(checkKillSwitch())return;delay(10);}
  Serial.readString();
  for(int i=5;i>=1;i--){Serial.printf("  %d...\n",i);delay(1000);}
  scale.set_scale(1.0f);scale.set_offset(0);delay(300);
  long fz=(long)readRawTrimmed(30);
  scale.set_offset(fz);scale.set_scale(nf);delay(500);
  if(fabsf(readStableWeight())>2.0f){
    scale.set_scale(1.0f);scale.set_offset(0);delay(300);
    fz=(long)readRawTrimmed(30);scale.set_offset(fz);scale.set_scale(nf);delay(500);
  }
  saveCalibrationToNVS(nf,fz);
  if(configId!=""&&WiFi.status()==WL_CONNECTED)
    httpPatch("/feeder_config?id=eq."+configId,
              "{\"calibration_factor\":"+String(nf,6)+",\"calibration_trigger\":false}");
  Serial.println("\n========================================");
  Serial.println("  CALIBRATION COMPLETE — place bowl, type T");
  Serial.println("========================================\n");
}
void loadCalibrationFromNVS(){
  prefs.begin(NVS_NAMESPACE,true);
  calDoneOnce=prefs.getBool(NVS_KEY_DONE,false);
  if(calDoneOnce){
    float f=prefs.getFloat(NVS_KEY_FACTOR,1.0f);long o=(long)prefs.getLong(NVS_KEY_OFFSET,0);prefs.end();
    scale.set_offset(o);scale.set_scale(f);delay(500);
    Serial.printf("[NVS] Cal: factor=%.6f offset=%ld\n",f,o);
  } else {
    prefs.end();scale.set_offset(0);scale.set_scale(1.0f);
    if(scale.is_ready()) scale.tare(10);
    Serial.println("[NVS] No calibration found.");
  }
}
void saveCalibrationToNVS(float factor,long offset){
  prefs.begin(NVS_NAMESPACE,false);
  prefs.putFloat(NVS_KEY_FACTOR,factor);prefs.putLong(NVS_KEY_OFFSET,offset);prefs.putBool(NVS_KEY_DONE,true);
  prefs.end();calDoneOnce=true;
  Serial.printf("[NVS] Cal saved: factor=%.6f offset=%ld\n",factor,offset);
}

// =============================================================================
// READING FUNCTIONS
// =============================================================================
float readRawTrimmed(int n){
  if(!scale.is_ready()) return 0;
  float* buf=new float[n];
  for(int i=0;i<n;i++){unsigned long t=millis();while(!scale.is_ready()&&millis()-t<500);buf[i]=(float)scale.read();delay(HX711_MIN_MS);}
  for(int i=1;i<n;i++){float k=buf[i];int j=i-1;while(j>=0&&buf[j]>k){buf[j+1]=buf[j];j--;}buf[j+1]=k;}
  int tr=max(1,(int)(n*TRIM_PERCENT));float s=0;int c=0;
  for(int i=tr;i<n-tr;i++){s+=buf[i];c++;}delete[]buf;
  return c>0?s/c:0.0f;
}
float takeTrimmedReading(int n){
  if(!scale.is_ready()) return currentWeight;
  float* buf=new float[n];
  for(int i=0;i<n;i++){unsigned long t=millis();while(!scale.is_ready()&&millis()-t<500);buf[i]=scale.get_units(1);delay(HX711_MIN_MS);}
  for(int i=1;i<n;i++){float k=buf[i];int j=i-1;while(j>=0&&buf[j]>k){buf[j+1]=buf[j];j--;}buf[j+1]=k;}
  int tr=max(1,(int)(n*TRIM_PERCENT));float s=0;int c=0;
  for(int i=tr;i<n-tr;i++){s+=buf[i];c++;}delete[]buf;
  return c>0?s/c:0.0f;
}
float readStableWeight(){float r=takeTrimmedReading(SAMPLES_NORMAL);return r<NOISE_FLOOR_G?0.0f:r;}

// =============================================================================
// REMOTE TARE / CALIBRATE
// =============================================================================
void remoteTare(){
  Serial.println("[Tare] Ensure platform is EMPTY.");delay(1000);
  float cf=scale.get_scale();
  scale.set_scale(1.0f);scale.set_offset(0);delay(300);
  long no=(long)readRawTrimmed(30);scale.set_offset(no);scale.set_scale(cf);delay(300);
  if(calDoneOnce){prefs.begin(NVS_NAMESPACE,false);prefs.putLong(NVS_KEY_OFFSET,no);prefs.end();}
  if(configId!=""&&WiFi.status()==WL_CONNECTED)
    httpPatch("/feeder_config?id=eq."+configId,"{\"tare_trigger\":false}");
  float after=readStableWeight();
  Serial.printf("[Tare] Done. %.2fg%s\n",after,fabsf(after)>3.0f?"  WARNING: not zero":"");
}
void remoteCalibrate(float kg){
  if(kg<=0) return;
  Serial.printf("[RemoteCal] %.1fg in 3s...\n",kg);delay(3000);
  scale.set_scale(1.0f);scale.set_offset(0);delay(300);
  long rz=(long)readRawTrimmed(20);scale.set_offset(rz);
  long rw=(long)readRawTrimmed(SAMPLES_CAL);long net=rw-rz;
  if(abs(net)<1000){Serial.println("[RemoteCal] ERROR: too small.");
    if(configId!="")httpPatch("/feeder_config?id=eq."+configId,"{\"calibration_trigger\":false}");return;}
  float nf=fabsf((float)net/kg);
  scale.set_offset(rz);scale.set_scale(nf);delay(500);
  float v=fabsf(takeTrimmedReading(10));
  Serial.printf("[RemoteCal] Factor=%.6f Verify=%.2fg Err=%.2f%%\n",nf,v,fabsf(v-kg)/kg*100.0f);
  scale.set_scale(1.0f);scale.set_offset(0);delay(300);
  long fz=(long)readRawTrimmed(20);scale.set_offset(fz);scale.set_scale(nf);delay(300);
  saveCalibrationToNVS(nf,fz);
  if(configId!=""&&WiFi.status()==WL_CONNECTED)
    httpPatch("/feeder_config?id=eq."+configId,
              "{\"calibration_factor\":"+String(nf,6)+",\"calibration_trigger\":false}");
}

// =============================================================================
// HELPERS
// =============================================================================
void stopMotor(){feederServo.attach(SERVO_PIN,500,2400);feederServo.write(SERVO_STOP);delay(100);feederServo.detach();}
bool checkKillSwitch(){return digitalRead(KILL_PIN)==LOW;}
void reportJam(){
  if(configId==""||WiFi.status()!=WL_CONNECTED){Serial.println("[JAM] Offline.");return;}
  httpPatch("/feeder_config?id=eq."+configId,"{\"jam_detected\":true,\"motor_enabled\":false}");
}

// =============================================================================
// HTTP HELPERS
// =============================================================================
String httpGet(String ep){
  if(WiFi.status()!=WL_CONNECTED) return "";
  HTTPClient h;h.begin(String(SB_URL)+ep);
  h.addHeader("apikey",SB_KEY);h.addHeader("Authorization",String("Bearer ")+SB_KEY);h.setTimeout(15000);
  int code=h.GET();String body=(code==200)?h.getString():"";
  if(code!=200)Serial.printf("[HTTP] GET %s -> %d\n",ep.c_str(),code);
  h.end();return body;
}
String httpPatch(String ep,String body){
  if(WiFi.status()!=WL_CONNECTED) return "";
  HTTPClient h;h.begin(String(SB_URL)+ep);
  h.addHeader("apikey",SB_KEY);h.addHeader("Authorization",String("Bearer ")+SB_KEY);
  h.addHeader("Content-Type","application/json");h.addHeader("Prefer","return=minimal");
  int code=h.sendRequest("PATCH",body);h.end();return String(code);
}
String httpPost(String ep,String body){
  if(WiFi.status()!=WL_CONNECTED) return "";
  HTTPClient h;h.begin(String(SB_URL)+ep);
  h.addHeader("apikey",SB_KEY);h.addHeader("Authorization",String("Bearer ")+SB_KEY);
  h.addHeader("Content-Type","application/json");h.addHeader("Prefer","return=minimal");
  int code=h.POST(body);h.end();return String(code);
}
