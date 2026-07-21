# AsapGuard Project Completion Summary

Updated: 2026-07-21 (Asia/Kuala_Lumpur)

## Project outcome

AsapGuard is a working ESP32-based kitchen air-monitoring and rule-based predictive ventilation prototype. It measures temperature, humidity, smoke/VOC behavior, and an MQ-6 digital gas signal, predicts deteriorating conditions from filtered trends, and automatically controls one or two ventilation fans.

The completed flow is:

```text
Sensors -> ESP32 predictive controller -> relays/fans
                    |
                    v
              Azure IoT Hub
                    |
                    v
         Azure Storage JSON batches
                    |
                    v
       Public read-only AsapGuard API
                    |
             +------+------+
             |             |
          Grafana       Telegram alerts
```

The ESP32 remains the immediate controller. Azure, Grafana, and Telegram are delayed monitoring paths and are not safety-control mechanisms.

## Hardware completed

- ESP32-WROOM-32 development board.
- DHT22 on GPIO4.
- MQ-135 analog input on GPIO34 through a 10k/15k voltage divider.
- MQ-6 active-low digital input on GPIO35 through a 10k/15k voltage divider.
- DS3231 RTC and 20x4 LCD on I2C GPIO21/GPIO22.
- Fan 1 and Fan 2 relay drivers on GPIO32/GPIO33.
- Two active-low 5 V relay modules controlled through 2N2222 transistor stages.
- Two 5 V fans powered through relay COM/NO contacts.
- USB power for ESP32 and regulated external 5 V for MQ heaters, relays, and fans.
- Common ground between the ESP32 and external supply.
- Hardware mounted in the miniature with LCD on the right, fans on the left, and sensors on top.

MQ-6 sensitivity was adjusted using its onboard potentiometer until the DOUT response worked. The two-fan Stage 2 actuation path was verified.

## Firmware completed

Current firmware version:

```text
0.4.0-predictive-rules
```

The controller uses rule-based prediction rather than machine learning. It includes:

- 20-sample environmental baseline.
- EMA filtering for MQ-135 and humidity.
- Filtered slopes and five-minute projections.
- Persistence to reject short noisy events.
- Approximately 15-second fan safety hold.
- Automatic and manual control modes.
- Immediate Stage 2 override from an active-low MQ-6 trigger.
- LCD display and Azure telemetry publishing.

Final rule thresholds:

| Rule | Threshold |
|---|---:|
| MQ-135 Stage 1 direct delta | +70 |
| MQ-135 Stage 2 direct delta | +150 |
| MQ-135 predictive gate | Current delta at least +25 and projected delta at least +70 |
| Humidity Stage 1 direct delta | +3.0% |
| Humidity predictive gate | Current delta at least +0.8%, slope above 0.20%/min, projected delta at least +3.0% |
| MQ-6 digital trigger | Immediate Stage 2 |

Temperature is displayed and published but does not trigger fan control.

## Functional tests completed

- Stable baseline and normal-air state: passed.
- Humidity-rise detection using damp tissue: passed.
- Humidity Stage 1 and Fan 1 activation: passed.
- MQ-135 smoke/VOC response: passed.
- Predictive MQ-135 warning behavior: passed.
- Direct MQ-135 Stage 1 response: passed.
- MQ-6 DOUT sensitivity adjustment: completed.
- Stage 2 and both-fan control path: passed.
- LCD, RTC, relay stages, and physical fans: passed.
- Azure telemetry publication and Storage routing: passed.

The room had poor ventilation and a higher MQ-135 ambient baseline. This was accepted as the room reference because the controller operates primarily on changes relative to its baseline.

## Azure completed

- Resource group: `asapguard-rg`.
- IoT Hub: `asapguard-hub`.
- Storage container: `telemetry`.
- ESP32 telemetry successfully routed from IoT Hub to Azure Storage JSON.
- Downloaded `08.json` was validated as five readable v0.4 records containing all 31 firmware telemetry fields.
- Later Azure blobs were verified, confirming Storage continued receiving telemetry after restart.
- Azure access uses Microsoft Entra identities and narrowly scoped `Storage Blob Data Reader` assignments. No account key or device key is used by the API.

## Public API completed

The API is deployed to Azure App Service on the LinuxFree tier with Node 24 LTS and a system-assigned managed identity.

Public endpoints:

```text
Base:    https://asapguard-api-28206.azurewebsites.net
Health:  https://asapguard-api-28206.azurewebsites.net/health
Latest:  https://asapguard-api-28206.azurewebsites.net/api/latest
History: https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60
```

The base URL returns API status and endpoint information. The API is read-only and publicly reachable.

API protections include:

- Explicit public-field allowlist.
- Removal of Azure envelope metadata and unexpected fields.
- Managed-identity Storage authentication.
- Configurable CORS origin allowlist.
- Bounded history queries.
- ETag caching so only new or changed blobs are downloaded after initial loading.
- Generic production errors.
- Telemetry freshness detection.

The deployed contract contains 38 fields: 31 firmware fields plus seven API-derived fields.

## Forecast and freshness fields

The API adds rule-based threshold-arrival estimates:

- `mq135Stage1EtaSeconds`
- `mq135Stage2EtaSeconds`
- `humidityStage1EtaSeconds`
- `nextThresholdEtaSeconds`
- `nextThresholdTarget`

ETA is calculated linearly from the current delta and filtered slope. It is limited to the firmware's five-minute prediction horizon, returns `0` after crossing, and returns `null` when conditions are not rising or no credible five-minute estimate exists.

The API also adds:

- `telemetryAgeSeconds`
- `telemetryStale`

Telemetry becomes stale after 180 seconds. Normal history is anchored to the actual current time to prevent overnight data from being treated as live. Historical panel design can use `anchor=latest`, but active alerts must not use it.

## Grafana and Telegram handoff

Grafana Infinity configuration:

- API: `https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60`
- Type: JSON.
- Parser: Backend -> JSONata.
- Root selector: `$.data`.
- Timestamp field: `timestampUtc`.
- Timestamp type: Time (UNIX s).

Fresh alert query:

```text
https://asapguard-api-28206.azurewebsites.net/api/history?minutes=3
```

Recommended current alert rules:

| Alert | Condition |
|---|---|
| Stage 1 | Last `fanStageCommand >= 1` |
| Stage 2 | Last `fanStageCommand >= 2` |
| Predictive warning | Last `predictedFanStage >= 1` |
| MQ-6 gas | Last `mq6Triggered == true` |
| Threshold approaching | `nextThresholdEtaSeconds <= 60` and not null |

Grafana evaluates alerts every 30-60 seconds. A 10-second evaluation cannot create 10-second telemetry because Azure Storage batches arrive roughly once per minute.

Telegram notifications are routed through Grafana Alertmanager. The bot token stays in Grafana's secure contact-point configuration and must never be placed in frontend JavaScript or the public API.

## Verification results

- Eight automated API/parser tests pass.
- Dependency audit reported zero known vulnerabilities at verification time.
- Live `/health`, `/api/latest`, and `/api/history` requests returned HTTP 200.
- Live API responses contained all 38 expected fields.
- CORS was verified for the configured localhost Grafana/dashboard development origin.
- The public base URL was verified with HTTP 200.

## Normal operating sequence

Startup:

1. Inspect wiring, relay terminals, and fan paths.
2. Connect ESP32 USB power first.
3. Confirm normal ESP32 boot.
4. Connect external regulated 5 V.
5. Allow MQ heaters to warm.
6. Restart with RST/EN if a fresh baseline is required.
7. Wait for `baselineSamples:20` and `baselineReady:true` before applying a stimulus.

Shutdown:

1. Stop all stimuli and ventilate.
2. Disconnect external 5 V first.
3. Disconnect ESP32 USB power second.

## Security and safety requirements

- Never share or commit `iot_configs.h`.
- Never share Wi-Fi passwords, Azure device keys, Storage account keys, connection strings, SAS tokens, or Telegram bot tokens.
- Never connect ESP32 GPIO35 directly to 5 V.
- Never combine lighter gas with flame.
- Never release butane continuously or inside an enclosure.
- Keep warm MQ cans, bare relay contacts, and fan blades clear of miniature material and loose wiring.
- Treat Grafana/Telegram as delayed monitoring only; local ESP32 fan control is the immediate response.

## Final status

The AsapGuard prototype, predictive firmware, two-stage fan control, Azure telemetry route, read-only API, threshold ETA fields, Grafana integration contract, Telegram alert path, miniature installation, and project documentation are complete and ready for demonstration.
