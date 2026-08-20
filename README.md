# 🐔 PoultryPal

### Smart Automated Poultry Feeding System

PoultryPal is a full-stack automated poultry feeding system designed for small and medium-sized poultry farms.

The system combines an **ESP32-S3 automated feeder**, **load-cell weight sensing**, **Supabase**, and a **React Progressive Web App (PWA)** to automatically dispense accurate feed portions while allowing farmers to monitor and manage feeding remotely in real time.

> **Repository:** `chicken-feeder`
> **Product:** `PoultryPal`

---

## 📸 Overview

PoultryPal connects a physical automated feeder to a web-based management dashboard.

```text
                    ┌─────────────────────────┐
                    │       PoultryPal        │
                    │       Web Dashboard     │
                    │       React + PWA       │
                    └────────────┬────────────┘
                                 │
                              Supabase
                                 │
              ┌──────────────────┴──────────────────┐
              │                                     │
       ┌──────▼──────┐                       ┌──────▼──────┐
       │  PostgreSQL │                       │  Realtime   │
       │   Database  │                       │   Updates   │
       └──────┬──────┘                       └─────────────┘
              │
              │ Wi-Fi
              │
       ┌──────▼─────────────────┐
       │       ESP32-S3         │
       │   PoultryPal Feeder    │
       ├────────────────────────┤
       │ HX711 + Load Cell      │
       │ Servo Motor             │
       │ DS3231 RTC              │
       │ NVS Offline Storage     │
       └────────────────────────┘
```

---

# ✨ Features

## 📊 Real-Time Dashboard

Monitor the feeder from the PoultryPal web application.

* 🥣 Live feed bowl weight
* 🪣 Hopper level
* 🟢 Device online/offline status
* ⏰ Next scheduled feeding
* 📈 Feeding history
* 🚨 Active alerts
* ⚙️ Feeder configuration

The dashboard receives live updates through Supabase Realtime instead of repeatedly polling the server.

---

## 🍽️ Two Feeding Modes

### Scheduled Feeding

Configure up to **8 feeding times per day**.

Each feeding schedule can specify the target amount of feed to dispense.

### Ad-Libitum Feeding

The feeder automatically dispenses feed when the bowl weight drops below a configured threshold.

This allows birds to access feed without requiring fixed feeding times.

---

## ⚖️ Weight-Verified Dispensing

PoultryPal uses a **load cell + HX711 amplifier** to measure the feed bowl.

Instead of simply running the motor for a fixed amount of time, the system monitors the actual weight and adjusts dispensing accordingly.

This helps provide more consistent feeding quantities.

---

## 🎯 Variable-Angle Dispensing

The dispensing mechanism adjusts its opening duration according to the requested feed quantity.

Small feed requests receive shorter dispensing times while larger requests receive longer dispensing times.

This improves accuracy, particularly for smaller portions.

---

## 🔧 Automatic Calibration

Load-cell calibration can be performed from the PoultryPal web dashboard.

The calibration workflow allows the user to:

1. Prepare the empty bowl.
2. Zero the scale.
3. Place a known weight on the bowl.
4. Enter the reference weight.
5. Calculate the calibration factor.
6. Save the calibration settings.

No firmware re-flashing is required.

---

## 🛠️ Smart Jam Recovery

PoultryPal monitors the dispensing mechanism for possible feed jams.

When a jam is detected, the feeder:

1. Detects that the expected weight change is not occurring.
2. Stops the normal dispensing cycle.
3. Attempts a gentle recovery movement.
4. Checks whether dispensing resumes.
5. Repeats recovery when necessary.
6. Raises an alert after repeated failures.

This helps prevent a temporary feed bridge from becoming a permanent feeding failure.

---

## 🐔 Flock-Aware Feed Targets

PoultryPal includes feed requirement data based on:

* Breed type
* Bird age
* Production type
* Daily feed requirement per bird

The system can use flock information to help determine appropriate daily feed targets.

Initial feed requirement data is seeded for Kenyan poultry production.

---

## 🪣 Hopper Tracking

PoultryPal tracks the estimated amount of feed remaining in the hopper.

The system:

* Records hopper refills.
* Deducts dispensed feed.
* Estimates remaining capacity.
* Detects potentially empty hoppers.
* Generates alerts when feed levels become critically low.

---

## 📈 Feeding History

Every feeding event can be recorded in Supabase.

The dashboard can display:

* Feeding time
* Target feed amount
* Actual dispensed amount
* Bowl weight
* Feeding status
* Feeding history
* Weight trends

This allows farmers to monitor feeding behavior over time.

---

## 🚨 Alerts

PoultryPal can generate alerts for important events such as:

* ⚠️ Feed dispensing jam
* 🪣 Empty/low hopper
* 📡 Offline feeder
* ❌ Repeated dispensing failure
* ⚙️ Device problems

The application can also support push notifications for important alerts.

---

## 📡 Offline Resilience

The feeder is designed to continue operating even when Wi-Fi or the Supabase backend becomes temporarily unavailable.

Feeding logs are temporarily stored in **ESP32 NVS flash**.

When connectivity is restored, the feeder synchronizes the stored records with Supabase.

```text
Normal operation

ESP32 → Supabase
   │
   └── Feeding logs synchronized


Internet unavailable

ESP32
 │
 └── NVS Flash
       │
       └── Store feeding logs


Internet restored

NVS Flash → ESP32 → Supabase
```

This prevents temporary network failures from causing feeding records to be lost.

---

# 🏗️ System Architecture

PoultryPal consists of three main layers.

### 1. Hardware Layer

The physical automated feeder.

* ESP32-S3
* HX711
* Load cell
* Servo motor
* DS3231 RTC
* Kill switch

### 2. Backend Layer

Supabase provides:

* PostgreSQL database
* Realtime synchronization
* Device configuration storage
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
| Kill Switch               | Emergency/manual shutdown   |

---

# 🔌 Pin Mapping

| Signal      | ESP32-S3 GPIO |
| ----------- | ------------: |
| HX711 DOUT  |        GPIO 4 |
| HX711 SCK   |        GPIO 5 |
| Servo       |        GPIO 6 |
| Kill Switch |        GPIO 2 |
| RTC SDA     |        GPIO 8 |
| RTC SCL     |        GPIO 9 |

> Always verify the GPIO assignments against your actual hardware revision before connecting or powering the system.

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
│
├── .env.example
├── package.json
├── vite.config.*
├── README.md
└── ...
```

The exact frontend folders may vary depending on the current implementation.

---

# 🚀 Getting Started

## 1. Clone the Repository

```bash
git clone https://github.com/kim-t-a/chicken-feeder.git
cd chicken-feeder
npm install
```

---

# 🗄️ 2. Set Up Supabase

Create a new project in Supabase.

Open the **SQL Editor** and run:

```text
supabase_setup.sql
```

The setup creates the main PoultryPal database tables.

### Main Tables

| Table               | Purpose                                     |
| ------------------- | ------------------------------------------- |
| `feeder_config`     | Current feeder configuration and live state |
| `feeding_logs`      | Feeding/dispensing events                   |
| `hopper_refills`    | Hopper refill history                       |
| `alert_logs`        | Jam, hopper and offline alerts              |
| `feed_requirements` | Feed requirements by breed and age          |

---

## 3. Enable Supabase Realtime

In Supabase, open:

**Database → Replication**

Enable Realtime for:

* `feeder_config`
* `feeding_logs`
* `alert_logs`

This allows the PoultryPal dashboard to receive changes immediately.

---

# 🔐 4. Configure Environment Variables

Create a `.env` file in the project root.

```env
VITE_SUPABASE_URL="your-supabase-project-url"
VITE_SUPABASE_ANON_KEY="your-supabase-anon-key"
```

For security, **never commit your `.env` file or private keys to GitHub.**

You can provide an example configuration through:

```text
.env.example
```

---

# 💻 5. Run the Web Application

Start the development server:

```bash
npm run dev
```

Create a production build:

```bash
npm run build
```

Preview the production build:

```bash
npm run preview
```

---

# 🔌 6. Flash the ESP32-S3 Firmware

Open:

```text
firmware/poultry_feeder/poultry_feeder.ino
```

You can use either:

* Arduino IDE
* PlatformIO

Install the required libraries:

```text
ArduinoJson
HX711
ESP32Servo
RTClib
```

Configure the firmware with your:

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

# 🔄 Feeding Process

### Scheduled Mode

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

### Ad-Libitum Mode

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

The feeder continuously checks whether the expected feed weight is changing during dispensing.

If the weight does not increase as expected:

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

# 📱 Progressive Web App

PoultryPal is designed as a Progressive Web App.

This allows users to install the dashboard on supported devices and use it like a standalone application.

The PWA can provide:

* Install-to-home-screen support
* Standalone application experience
* Responsive mobile interface
* Real-time updates
* Offline-friendly application behavior

---

# 🌐 Technology Stack

## Hardware

* ESP32-S3
* HX711
* Load Cell
* Servo Motor
* DS3231 RTC

## Firmware

* Arduino C++
* ArduinoJson
* HX711 library
* ESP32Servo
* RTClib
* NVS Flash

## Frontend

* React
* TypeScript
* Vite
* Progressive Web App

## Backend

* Supabase
* PostgreSQL
* Supabase Realtime

---

# 🔒 Security

The application should follow these security principles:

* Keep secrets out of source control.
* Use environment variables for frontend configuration.
* Configure appropriate Supabase Row Level Security policies.
* Never expose Supabase service-role keys in the frontend.
* Protect device authentication credentials.
* Validate data received from the ESP32.

---

# 📊 Future Improvements

Potential future improvements include:

* [ ] Multi-feeder management
* [ ] Multiple farm/house management
* [ ] Advanced feed consumption analytics
* [ ] Automatic abnormal consumption detection
* [ ] Mobile notifications
* [ ] Farmer accounts and authentication
* [ ] Farm-level reports
* [ ] Feed cost tracking
* [ ] Solar-powered feeder support
* [ ] OTA ESP32 firmware updates
* [ ] Remote device diagnostics
* [ ] AI-powered feeding recommendations

---

# 🤝 Contributing

Contributions, suggestions, and improvements are welcome.

If you would like to contribute:

```bash
git clone https://github.com/kim-t-a/chicken-feeder.git
cd chicken-feeder
npm install
```

Create a branch for your changes:

```bash
git checkout -b feature/your-feature
```

Make your changes, test them, and submit a pull request.

---

# 📄 License

This project is licensed under the Apache License 2.0.

---

# 🐔 About PoultryPal

PoultryPal was developed to make automated poultry feeding more accessible to small and medium-scale poultry farmers.

By combining **embedded systems, weight sensing, cloud connectivity, automation, and a real-time web application**, PoultryPal aims to reduce feed wastage, improve feeding consistency, and make poultry farm management easier.

**PoultryPal — Smarter feeding. Better farming.**
