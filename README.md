💧 Wireless Water Management System (ESP32 + ESP8266)

A robust, offline, Wi-Fi based water control system designed for real-time response, stability, and long-term operation.

The system uses distributed controllers communicating over a local Wi-Fi network without any internet or cloud dependency.

📌 Project Overview

This project implements a wireless inlet–outlet water control system using:

ESP32 (Master Controller)

ESP8266 (Inlet Node)

ESP8266 (Outlet Node)

Communication is handled using Wi-Fi SoftAP + HTTP, ensuring deterministic, low-latency control in completely offline environments.

🧠 System Architecture

                ┌────────────────────────────┐
                │        ESP32 MASTER         │
                │────────────────────────────│
                │ • Wi-Fi SoftAP (192.168.4.1)
                │ • Dual-Core FreeRTOS        │
                │ • Control Logic             │
                │ • OLED Dashboard            │
                │ • Encoder + Button          │
                │ • Soil Sensor               │
                │ • Water Level Sensor        │
                └─────────────┬──────────────┘
                              │
          HTTP GET /open, /close (ACK-based)
                              │
        ┌─────────────────────┴─────────────────────┐
        │                                           │
┌──────────────────────┐                ┌──────────────────────┐
│   ESP8266 INLET NODE  │                │  ESP8266 OUTLET NODE │
│──────────────────────│                │──────────────────────│
│ IP: 192.168.4.2      │                │ IP: 192.168.4.3      │
│ • Relay (Inlet Valve)│                │ • Relay (Outlet Valve)
│ • HTTP Server        │                │ • HTTP Server        │
│ • /open              │                │ • /open              │
│ • /close             │                │ • /close             │
│ • Fail-safe Close    │                │ • Stable Executor    │
└──────────────────────┘                └──────────────────────┘

🔹 ESP32 – Master Controller

Acts as the central controller of the system.

Responsibilities:

Creates a Wi-Fi SoftAP

Reads:

Soil moisture sensor

Water level sensor

Rotary encoder

Mode selection button

Executes control logic:

OFF

AUTO

NORMAL

Sends commands to inlet and outlet nodes

Displays real-time system status on OLED

Stores configuration in non-volatile memory

Uses dual-core FreeRTOS task separation

🔹 ESP8266 – Inlet Node

Acts as a command-driven actuator.

Responsibilities:

Connects to ESP32 SoftAP

Controls inlet valve via relay

Exposes HTTP endpoints:

/open

/close

Sends immediate HTTP 200 OK acknowledgment

Includes fail-safe auto-close if communication is lost

🔹 ESP8266 – Outlet Node

Same actuator model as inlet node.

Responsibilities:

Controls outlet / drain valve

HTTP endpoints:

/open

/close

Immediate acknowledgment

Lightweight and stable firmware

🌐 Network Configuration
Device	IP Address
ESP32 Master	192.168.4.1
ESP8266 Inlet	192.168.4.2
ESP8266 Outlet	192.168.4.3

Wi-Fi Mode:

ESP32 → SoftAP

ESP8266 → Station

🖥 OLED Dashboard Layout
MODE : AUTO    TH: 70
WATER: HIGH
------------------------
SOIL : MOIST (1450)
------------------------
IN  : ON   OK
OUT : OFF  --

OLED Characteristics

Event-driven updates (oledDirty)

No delay() usage

No flicker

Instant refresh on:

Mode change

Encoder input

Sensor updates

Valve and connectivity status

⚙️ Control Logic
AUTO Mode

Inlet opens when:

Water level is LOW

Inlet closes when:

Water level becomes HIGH

Outlet opens if water remains HIGH continuously for a defined duration

NORMAL Mode

Manual or extended control logic (expandable)

OFF Mode

All valves closed (safe state)

🔒 Safety & Reliability Features

✔ Acknowledgment-based valve control
✔ Online / offline node detection
✔ Fail-safe valve closing
✔ No heap fragmentation
✔ Non-blocking UI
✔ Watchdog-safe firmware
✔ Designed for 24/7 operation

🧠 Dual-Core Task Design (ESP32)
Core	Task
Core 0	UI Task (OLED, Encoder, Button)
Core 1	Control Task (Sensors, Logic, Wi-Fi)

This separation ensures:

Smooth OLED performance

No UI delay due to networking

Predictable system behavior

🔌 Relay Configuration

Relay Type: ACTIVE-LOW

Default State: CLOSED

Safety Behavior: Auto close on communication loss

🧰 Technologies Used

ESP32 / ESP8266

FreeRTOS (ESP32)

Wi-Fi SoftAP

HTTP (REST-style)

OLED SSD1306

Preferences (NVS)

Hardware interrupts (rotary encoder)

🚀 How to Use

Flash ESP32 Master firmware

Flash ESP8266 Inlet firmware

Flash ESP8266 Outlet firmware

Power up ESP32 (SoftAP starts)

Power inlet and outlet nodes

System becomes operational automatically

🔧 Possible Extensions

/status feedback endpoint

Soil-based automatic irrigation logic

Valve runtime limits

Heartbeat-based node monitoring

Web configuration interface

Data logging

📄 License

Open-source.
Free to use, modify, and extend for education, research, and industrial projects.


┌────────────────────────┐
│ MODE : AUTO    TH: 70  │  ← mode + threshold together
│ WATER: HIGH           │
├────────────────────────┤
│ SOIL : MOIST (1450)    │
├────────────────────────┤
│ IN  : ON   ●           │
│ OUT : OFF  ○           │
└────────────────────────┘

**PIN CONNECTION**

| Board Pin Name | GPIO No. | Connected Device          | Purpose in Code      |
| -------------- | -------- | ------------------------- | -------------------- |
| **D21**        | GPIO 21  | OLED SDA                  | I2C Data             |
| **D22**        | GPIO 22  | OLED SCL                  | I2C Clock            |
| **D32**        | GPIO 32  | Rotary Encoder CLK        | Encoder interrupt    |
| **D33**        | GPIO 33  | Rotary Encoder DT         | Encoder direction    |
| **D26**        | GPIO 26  | Encoder Push Button       | MODE button          |
| **D34**        | GPIO 34  | Soil Moisture Sensor (AO) | Analog soil input    |
| **D27**        | GPIO 27  | Water Level Sensor (DO)   | Digital water detect |
| **D2**         | GPIO 2   | AUTO Mode LED             | AUTO indicator       |
| **D4**         | GPIO 4   | MANUAL Mode LED           | MANUAL indicator     |
| **3V3**        | —        | OLED / Sensors / Encoder  | Power                |
| **GND**        | —        | All modules               | Common ground        |
