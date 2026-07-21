# AsapGuard

AsapGuard is an ESP32-based kitchen air-monitoring and predictive ventilation prototype. It measures temperature, humidity, smoke/VOC behavior, and an MQ-6 digital gas signal, then uses transparent rule-based forecasting to control one or two ventilation fans.

The system combines embedded control, Azure IoT telemetry, a public read-only API, Grafana visualization, and Telegram alerts. The ESP32 remains the immediate control path; cloud monitoring is intentionally treated as delayed and non-safety-critical.

## Architecture

```text
DHT22 + MQ-135 + MQ-6 + RTC
              |
              v
     ESP32 predictive rules ------> Relay drivers ------> Fan 1 / Fan 2
              |
              v
        Azure IoT Hub
              |
              v
     Azure Storage JSON
              |
              v
      AsapGuard read-only API
          |              |
       Grafana       Telegram alerts
```

## Highlights

- Twenty-sample room baseline with EMA-filtered humidity and MQ-135 readings.
- Five-minute trend projections with persistence and a fan safety hold.
- Stage 1 ventilation for humidity or smoke/VOC deterioration.
- Stage 2 ventilation for high MQ-135 delta or MQ-6 digital gas detection.
- 20x4 LCD, DS3231 RTC, Azure IoT publishing, and Storage history.
- Managed-identity Azure API with an explicit public-field allowlist.
- Threshold-arrival estimates and stale-telemetry protection for Grafana.
- Telegram notifications through Grafana Alertmanager.

This is a **rule-based predictive ventilation controller**, not a machine-learning model. The rules are intentionally explainable and appropriate for the available classroom stimuli and dataset.

## Live API

- Base: [https://asapguard-api-28206.azurewebsites.net](https://asapguard-api-28206.azurewebsites.net)
- Latest: [https://asapguard-api-28206.azurewebsites.net/api/latest](https://asapguard-api-28206.azurewebsites.net/api/latest)
- History: [https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60](https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60)
- Health: [https://asapguard-api-28206.azurewebsites.net/health](https://asapguard-api-28206.azurewebsites.net/health)

The API is public and read-only. Telemetry is considered stale after three minutes, and Azure Storage batching introduces roughly one minute of monitoring latency.

## Repository layout

```text
firmware/AsapGuard_Azure_Test/  Current ESP32 + Azure firmware
api/                            Node.js read-only telemetry API
AsapGuard_Predictive_Rules.ino  Predictive-controller reference sketch
grafana-telegram-handoff.md     Dashboard and alert configuration
lecture-room-runbook.md         Calibration, installation, and demo checklist
asapguard-completion-summary-*  Full project outcome and verification
```

Raw telemetry exports, build artifacts, local dependencies, and private configuration are deliberately excluded.

## Hardware

- ESP32-WROOM-32 development board
- DHT22 temperature/humidity sensor on GPIO4
- MQ-135 analog output to GPIO34 through a 10k/15k divider
- MQ-6 active-low digital output to GPIO35 through a 10k/15k divider
- DS3231 RTC and 20x4 I2C LCD on GPIO21/GPIO22
- Two active-low 5 V relays driven through 2N2222 transistor stages
- Fan control on GPIO32/GPIO33
- USB power for ESP32; regulated external 5 V for MQ heaters, relays, and fans
- Common ground between both power domains

## Firmware rules

| Rule | Threshold |
|---|---:|
| MQ-135 Stage 1 direct delta | +70 |
| MQ-135 Stage 2 direct delta | +150 |
| MQ-135 predictive gate | Current delta >= +25 and projected delta >= +70 |
| Humidity Stage 1 direct delta | +3.0% |
| Humidity predictive gate | Current delta >= +0.8%, slope > 0.20%/min, projected delta >= +3.0% |
| MQ-6 active-low trigger | Immediate Stage 2 |

Temperature is displayed and published but is not a fan trigger.

## Firmware setup

1. Open `firmware/AsapGuard_Azure_Test/AsapGuard_Azure_Test.ino` in Arduino IDE.
2. Copy `iot_configs.example.h` to `iot_configs.h`.
3. Fill the local copy with Wi-Fi and Azure IoT device values.
4. Install the ESP32 and required sensor/LCD libraries.
5. Select `ESP32 Dev Module`, compile, and upload.
6. Keep `iot_configs.h` private; it is ignored by Git.

Serial commands at 115200 baud:

| Command | Action |
|---|---|
| `A` | Automatic predictive control |
| `0` | Manual: both fans off |
| `1` | Manual: Fan 1 on |
| `2` | Manual: both fans on |
| `N/H/S/G/U` | Normal/Humidity/Smoke/Butane/Unlabeled demo label |

## API development

```powershell
cd api
npm install
npm test
$env:TELEMETRY_SOURCE='local'
$env:LOCAL_TELEMETRY_PATH='path-to-an-azure-json-export'
npm start
```

See [api/README.md](api/README.md) for Azure managed-identity deployment and the complete API contract.

## Grafana and Telegram

Grafana Infinity should use JSON with the Backend JSONata parser, root selector `$.data`, and `timestampUtc` typed as `Time (UNIX s)`. Active alerts should query a fresh three-minute window and use controller outputs such as `fanStageCommand`, `predictedFanStage`, and `mq6Triggered` rather than old absolute sensor thresholds.

See [grafana-telegram-handoff.md](grafana-telegram-handoff.md) for the complete field map, alert conditions, ETA fields, freshness handling, and Telegram routing.

## Validation

- Normal, humidity, MQ-135 smoke/VOC, MQ-6, one-fan, and two-fan paths verified.
- Azure IoT Hub to Storage routing verified after ESP32 restart.
- Public API verified against live Azure Storage.
- Eight API/parser tests passing.
- Dependency audit reported zero known vulnerabilities at final verification.

## Safety

- Never combine lighter gas with flame or release gas continuously.
- Never test butane in an enclosure or poorly ventilated location.
- Never connect GPIO35 directly to 5 V.
- Keep warm MQ cans, relay contacts, fan blades, and bare conductors clear of miniature material.
- Startup: ESP32 USB first, then external 5 V.
- Shutdown: external 5 V first, then ESP32 USB.
- Do not treat Grafana or Telegram as an immediate safety system.

## Documentation

- [Project completion summary](asapguard-completion-summary-2026-07-21.md)
- [Full engineering handoff](asapguard-full-handoff-2026-07-21.md)
- [Lecture-room runbook](lecture-room-runbook.md)
- [Grafana and Telegram handoff](grafana-telegram-handoff.md)

## Contributions

This repository contains the integrated AsapGuard prototype. Hardware integration, predictive firmware, Azure telemetry, and the public API were developed as part of the main embedded/cloud work. Grafana dashboard and Telegram alert configuration were completed as a team integration. Add teammate names and precise contribution credits before presenting the repository publicly.
