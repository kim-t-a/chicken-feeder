# 🐔 PoultryPal

### Smart Automated Poultry Feeding System

PoultryPal is a full-stack automated feeding system for small and medium poultry farms. An ESP32-S3-based feeder dispenses precise, weight-verified portions of feed on a schedule or in ad-libitum mode, while a React dashboard lets farmers monitor, configure, and manage the feeder in real time.

> **Product:** PoultryPal
> **Repository:** chicken-feeder

---

# 🚀 How It Works

PoultryPal connects a physical automated feeder to a web-based management dashboard.

```text
┌─────────────────────────┐
│       PoultryPal        │
│     React Web App       │
│          PWA            │
└────────────┬────────────┘
             │
             │ Realtime
             │
       ┌─────▼──────┐
       │  Supabase  │
       │ PostgreSQL │
       │  Realtime  │
       └─────┬──────┘
             │
             │ Wi-Fi
             │
┌────────────▼────────────┐
│        ESP32-S3         │
│    PoultryPal Feeder    │
├─────────────────────────┤
│ HX711 + Load Cell       │
│ Servo Motor             │
│ DS3231 RTC              │
│ NVS Offline Storage     │
└─────────────────────────┘
```

The ESP32-S3 weighs the feed bowl, dispenses feed according to the configured feeding mode, detects and recovers from jams, and synchronizes feeding data with Supabase.

The web application reads and writes the same Supabase data in real time, allowing farmers to monitor and control the feeder remotely.

---

# ✨ Features

### 📊 Real-Time Dashboard

Monitor the feeder from the PoultryPal web application.

* 🥣 Live bowl weight
* 🪣 Hopper level
* 🟢 Device online/offline status
* ⏰ Next scheduled feeding
* 📈 Feeding history
* 🚨 Active alerts
* ⚙️ Feeder configuration

---

### 🍽️ Two Feeding Modes

#### Scheduled Feeding

Configure up to **8 feeding times per day** with configurable feed quantities.

#### Ad-Libitum Feeding

The feeder automatically dispenses feed when the bowl weight falls below a configured threshold.

---

### ⚖️ Weight-Verified Dispensing

PoultryPal uses a **load cell + HX711 amplifier** to measure the actual feed weight.

The feeder does not rely only on motor runtime. It continuously monitors the bowl weight during dispensing to help achieve more accurate portions.

---

### 🔧 Automatic Calibration

The load cell can be calibrated directly from the web dashboard without reflashing the ESP32.

The guided calibration process allows the user to:

1. Empty the feed bowl.
2. Zero the scale.
3. Place a known weight on the bowl.
4. Enter the reference weight.
5. Calculate the calibration factor.
6. Save the calibration settings.

---

### 🛠️ Smart Jam Recovery

PoultryPal detects possible dispensing jams by monitoring the expected change in feed weight.

When a jam occurs, the feeder:

1. Detects that the expected weight change is not occurring.
2. Stops normal dispensing.
3. Attempts a gentle recovery movement.
4. Checks whether dispensing resumes.
5. Retries when necessary.
6. Raises an alert after repeated failures.

---

### 🎯 Variable-Angle Dispensing

The dispensing mechanism adjusts its opening time according to the requested feed quantity.

This provides better control over feed portions, especially for smaller quantities.

---

### 🐔 Flock-Aware Feed Targets

PoultryPal includes feed requirement data based on:

* Breed
* Bird age
* Production type
* Daily feed requirement per bird

The system is seeded with feed requirement data suitable for Kenyan poultry production.

---

### 🪣 Hopper Tracking

PoultryPal tracks the estimated amount of feed remaining in the hopper.

The system:

* Records hopper refills.
* Deducts dispensed feed.
* Estimates remaining capacity.
* Detects low or empty hopper conditions.
* Generates alerts when feed levels become critical.

---

### 📈 Feeding History & Charts

Every feeding event can be recorded in Supabase.

The dashboard can display:

* Feeding time
* Target feed amount
* Actual dispensed amount
* Bowl weight
* Feeding status
* Feeding history
* Weight trends

---

### 🚨 Alerts

PoultryPal can generate alerts for:

* ⚠️ Jam detection
* 🪣 Low or empty hopper
* 📡 Offline device
* ❌ Repeated dispensing failure
* ⚙️ Device problems

Push notifications can also be supported for important alerts.

---

### 📡 Offline Resilience

The feeder continues operating even when Wi-Fi or Supabase is temporarily unavailable.

Feeding logs are stored locally in **ESP32 NVS flash** and synchronized with Supabase when the connection is restored.

```text
Internet Available

ESP32
  │
  └──────► Supabase
              │
              └── Feeding logs
```

```text
Internet Unavailable

ESP32
  │
  └──────► NVS Flash
              │
              └── Store feeding logs
```

```text
Internet Restored

NVS Flash
    │
    ▼
  ESP32
    │
    ▼
  Supabase
```

This prevents temporary network failures from causing feeding records to be lost.

---

### 📱 Installable PWA

The PoultryPal dashboard is built as a Progressive Web App.

Users can install it on supported devices and use it as a standalone application.

---

# 🏗️ System Architecture

PoultryPal consists of three main layers.

### 1. Hardware Layer

The physical automated feeding system:

* ESP32-S3
* HX711
* Load Cell
* Servo Motor
* DS3231 RTC

### 2. Backend Layer

Supabase provides:

* PostgreSQL database
* Realtime synchronization
* Device configuration
* Feeding logs
* Hopper records
* Alert records
* Feed requirement data

### 3. Web Application

The React PWA provides:

* Dashboard
* Feeding configuration
* Calibration
* Feeding history
* Charts
* Alerts
* Device monitoring
* Hopper management

---

# 🧰 Hardware

| Component                 | Purpose                     |
| ------------------------- | --------------------------- |
| ESP32-S3                  | Main microcontroller        |
| HX711                     | Load-cell amplifier         |
| Load Cell                 | Measures feed weight        |
| Continuous-Rotation Servo | Drives dispensing mechanism |
| DS3231 RTC                | Maintains accurate time     |

---

# 🔌 Pin Mapping

| Signal     | ESP32-S3 GPIO |
| ---------- | ------------- |
| HX711 DOUT | GPIO 4        |
| HX711 SCK  | GPIO 5        |
| Servo      | GPIO 6        |
| RTC SDA    | GPIO 8        |
| RTC SCL    | GPIO 9        |

> ⚠️ Verify the GPIO assignments against your actual hardware before connecting or powering the system.

---

# 📁 Project Structure

```text
chicken-feeder/
│
├── firmware/
│   └── poultry_feeder/
│       └── poultry_feeder.ino
│
├── src/
│   ├── components/
│   ├── pages/
│   ├── hooks/
│   ├── lib/
│   └── ...
│
├── public/
│
├── supabase_setup.sql
├── .env.example
├── package.json
├── vite.config.*
└── README.md
```

---

# 🚀 Getting Started

## 1. Clone the Repository

```bash
git clone https://github.com/kim-t-a/chicken-feeder.git
cd chicken-feeder
npm install
```

---

## 2. Set Up Supabase

Create a project at [Supabase](https://supabase.com/).

Open the **SQL Editor** and run:

```text
supabase_setup.sql
```

This creates the main PoultryPal database tables.

### Database Tables

| Table               | Purpose                                                   |
| ------------------- | --------------------------------------------------------- |
| `feeder_config`     | Current mode, schedule, calibration and live feeder state |
| `feeding_logs`      | Every feeding and dispensing event                        |
| `hopper_refills`    | Hopper refill history                                     |
| `alert_logs`        | Jam, hopper and offline alerts                            |
| `feed_requirements` | Daily feed requirements by breed and age                  |

---

## 3. Enable Supabase Realtime

In Supabase, go to:

**Database → Replication**

Enable Realtime for:

* `feeder_config`
* `feeding_logs`
* `alert_logs`

This allows the PoultryPal dashboard to receive changes in real time without continuous frontend polling.

---

## 4. Configure Environment Variables

Create a `.env` file in the project root:

```env
VITE_SUPABASE_URL="your-supabase-project-url"
VITE_SUPABASE_ANON_KEY="your-supabase-anon-key"
```

> ⚠️ Never commit your `.env` file or Supabase service-role key to GitHub.

---

## 5. Run the Web Application

Start the development server:

```bash
npm run dev
```

Build the production application:

```bash
npm run build
```

Preview the production build:

```bash
npm run preview
```

---

## 6. Flash the ESP32-S3 Firmware

Open:

```text
firmware/poultry_feeder/poultry_feeder.ino
```

The firmware can be flashed using **Arduino IDE** or **PlatformIO**.

Install the required libraries:

```text
ArduinoJson
HX711
ESP32Servo
RTClib
```

Configure the firmware with:

* Wi-Fi credentials
* Supabase URL
* Supabase API key

Then compile and flash the firmware to the ESP32-S3.

---

# ⏰ Device Startup

When the feeder starts, it performs the following process:

```text
ESP32-S3 Boot
      ↓
Initialize hardware
      ↓
Initialize load cell
      ↓
Initialize RTC
      ↓
Load saved configuration
      ↓
Connect to Wi-Fi
      ↓
Synchronize time using NTP
      ↓
Connect to Supabase
      ↓
Synchronize feeder configuration
      ↓
Start feeding controller
      ↓
Monitor weight + schedule + hopper
```

---

# 🍽️ Feeding Process

## Scheduled Mode

```text
Check current time
       ↓
Scheduled feeding time?
       ↓
      YES
       ↓
Read bowl weight
       ↓
Calculate required feed
       ↓
Open dispensing mechanism
       ↓
Monitor weight
       ↓
Target reached?
       ↓
Close mechanism
       ↓
Save feeding log
       ↓
Sync with Supabase
```

---

## Ad-Libitum Mode

```text
Monitor bowl weight
       ↓
Weight below threshold?
       ↓
      YES
       ↓
Dispense feed
       ↓
Monitor weight
       ↓
Target reached
       ↓
Save feeding log
```

---

# 🛠️ Jam Recovery

```text
Possible jam detected
        ↓
Stop dispensing
        ↓
Attempt recovery movement
        ↓
Check weight again
        ↓
Recovered?
    ┌────┴────┐
   YES        NO
    ↓          ↓
 Continue    Retry
 feeding       ↓
            Repeated failure?
                 ↓
                YES
                 ↓
            Create alert
```

---

# 🔄 Offline Synchronization

When the internet is unavailable:

```text
ESP32
  ↓
NVS Flash
  ↓
Store feeding events
  ↓
Continue operation
```

When connectivity returns:

```text
NVS Flash
    ↓
  ESP32
    ↓
  Supabase
    ↓
Feeding history updated
```

---

# 🌐 Technology Stack

### Hardware

* ESP32-S3
* HX711
* Load Cell
* Continuous-Rotation Servo
* DS3231 RTC

### Firmware

* Arduino C++
* ArduinoJson
* HX711
* ESP32Servo
* RTClib
* ESP32 NVS

### Frontend

* React
* TypeScript
* Vite
* Progressive Web App

### Backend

* Supabase
* PostgreSQL
* Supabase Realtime

---
<p align="center">
  <img src="poultry.jpg" alt="PoultryPal Automated Poultry Feeder" width="700">
</p>
