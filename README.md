PoultryPal is a full-stack automated feeding system for small and medium poultry farms. An ESP32-S3-based feeder dispenses precise, weight-verified portions of feed on a schedule (or ad-lib), and a React dashboard lets you monitor, configure, and log everything in real time via Supabase
**How it works**
The firmware weighs the feed bowl, dispenses on a schedule (or when weight drops below an ad-lib threshold), detects jams and recovers from them, and pushes live weight + status to Supabase every few seconds.
The web app is a PWA that reads/writes the same Supabase tables in real time — no polling needed on the frontend, and the ESP32 buffers logs in NVS flash if it loses WiFi so nothing is lost.
**Features**
Real-time dashboard — live bowl weight, hopper level, device online/offline status, next scheduled feed
Two feeding modes — scheduled (up to 8 feed times/day) or ad-lib (dispenses when the bowl drops below a threshold)
Auto-calibration — guided load-cell calibration flow from the web UI, no re-flashing required
Smart jam recovery — detects a stalled flap, gently nudges to clear a bridge, and only raises an alert after repeated failures
Variable-angle dispensing — flap opening time scales with the target gram amount for more precise small dispenses
Flock-aware feed targets — built-in daily feed requirement tables by breed (broiler/layer) and age, seeded for Kenyan poultry standards
Hopper tracking — logs refills and auto-deducts dispensed feed to estimate remaining hopper capacity
Feeding history & charts — logs of every dispense with weight-over-time visualization
Alerts — jam detection, empty hopper, and offline-device alerts, with push notifications
Offline resilience — feeder buffers feed logs locally (NVS) and syncs once WiFi/Supabase is reachable again
Installable PWA — add to home screen, works as a standalone app
**Getting started**
1. Clone the repo
bash
git clone https://github.com/kim-t-a/chicken-feeder.git
cd chicken-feeder
npm install
**Set up Supabase**
Create a project at supabase.com.
Open the SQL Editor and run everything in supabase_setup.sql. This creates:
feeder_config — current mode, schedule, calibration, live bowl weight, alert state
feeding_logs — every dispense event
hopper_refills — hopper top-up history
alert_logs — jam / empty hopper / offline history
feed_requirements — daily feed grams per bird by breed and age (pre-seeded)
Go to Database → Replication and enable Realtime for feeder_config, feeding_logs, and alert_logs.
**Configure environment variables**
Create a .env file in the project root
VITE_SUPABASE_URL='your-supabase-project-url'
VITE_SUPABASE_ANON_KEY='your-supabase-anon-key'
4.**Run the web app**
npm run dev       # local dev server
npm run build      # production build
npm run preview    # preview the production build
5.**Flash the firmware**
Open firmware/poultry_feeder/poultry_feeder.ino in the Arduino IDE (or PlatformIO).
Install the required libraries: ArduinoJson, HX711, ESP32Servo, RTClib.
Fill in your WiFi credentials and Supabase URL/anon key at the top of the file.
Wire up the hardware (see below) and flash to an ESP32-S3.
On boot, the feeder syncs time over NTP, connects to Supabase, and starts polling for schedule/config changes.
6.**Hardware**
Component	Purpose
ESP32-S3	Micro controller
HX711 + load cell	Weighs the feed bowl
DS3231 RTC	Keeps accurate time for scheduled feeds, even offline
Continuous-rotation servo	Drives the dispense
**Pin mapping**
Signal	   GPIO
HX711 DOUT	4
HX711 SCK	  5
Servo	      6
Kill switch	2
RTC SDA	    8
RTC SCL	    9
