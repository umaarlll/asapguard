# AsapGuard Full Project Handoff

Updated: 2026-07-21 (Asia/Kuala_Lumpur)

## Immediate instruction for the next Codex task

Continue from this document. First verify the downloaded Azure Storage file `C:\Users\umaar\Downloads\08.json`. Then continue in this order:

1. Verify Azure JSON storage and current telemetry schema.
2. Build a safe shared JSON API for dashboard/Telegram use.
3. Perform final lecture-room calibration and MQ-6 sensitivity check.
4. Install the verified hardware into the kitchen miniature.
5. Run the final demo checklist.

Do not request, display, copy, or commit any Wi-Fi password, Azure device key, storage account key, shared-access key, or the contents of `iot_configs.h`.

## Project purpose

AsapGuard is an ESP32-based kitchen air monitoring and predictive ventilation classroom prototype. It reads temperature, humidity, smoke/VOC level, and an MQ-6 digital gas signal. A rule-based predictive controller estimates whether air conditions are deteriorating and commands one or two fans through relay modules.

This is deliberately described as a **rule-based predictive ventilation controller**, not as machine learning. Random Forest/LSTM work was rejected as unnecessary and unreliable for the limited classroom stimuli and dataset.

Planned demonstrations:

- Normal room air
- Humidity using a damp tissue
- Smoke/VOC using vape or extinguished candle smoke
- Butane/lighter gas, with strict safety limits
- Automatic one-fan and two-fan ventilation
- LCD and Azure telemetry
- Later dashboard and optional Telegram notification

## Hardware

- ESP32-WROOM-32 development board, Arduino IDE board profile `ESP32 Dev Module`
- DHT22 on GPIO4
- DS3231 RTC on I2C
- AT24C32 EEPROM on the RTC module
- 20x4 I2C LCD at address `0x27`
- I2C SDA GPIO21, SCL GPIO22
- MQ-135 analog output through a voltage divider to GPIO34
- MQ-6 digital output through a voltage divider to GPIO35
- Two 5 V Songle single-channel active-low relay modules
- Two 5 V fans
- Two `2N2222A H331` NPN transistor relay drivers
- External regulated 5 V adapter for MQ sensors, relays, and fans
- USB power for ESP32
- All grounds are common

## Breadboard naming

- Left red/blue rails: external 5 V supply
- Right red/blue rails: ESP32-side power/reference rails
- External 5 V negative and ESP32 GND are connected as a common ground

## Sensor wiring

### DHT22

- VCC to ESP32 3.3 V rail
- GND to common ground
- DATA to GPIO4

### DS3231 and LCD

- SDA to GPIO21
- SCL to GPIO22
- Shared I2C wiring was tested successfully
- I2C scan previously found RTC devices at `0x68` and `0x57`
- LCD scan found `0x27`

### MQ-135 divider

```text
MQ-135 A0 --- 10k ohm --- junction --- GPIO34
                              |
                            15k ohm
                              |
                             GND
```

- MQ-135 VCC to external 5 V
- MQ-135 GND to common ground
- Normal readings have varied with room air and airflow, usually roughly 300-400 after warm-up

### MQ-6 digital divider

```text
MQ-6 D0 --- 10k ohm --- junction --- GPIO35
                            |
                          15k ohm
                            |
                           GND
```

- MQ-6 VCC to external 5 V
- MQ-6 GND to common ground
- Digital logic is active-low: GPIO35 LOW means triggered
- The digital input and software path were previously verified by temporarily pulling the GPIO35 divider junction directly to GND
- Real butane did not activate the MQ-6 DOUT LED in the vape-heavy home environment; the module potentiometer needs recalibration in cleaner air
- Do not connect GPIO35 to 5 V

## Relay transistor drivers

Direct ESP32-to-relay control failed because the 5 V active-low relay input did not reliably turn off with a 3.3 V GPIO HIGH. Two NPN transistor drivers fixed this.

For the supplied `2N2222A H331`, with the flat face toward the user, the working orientation was:

- Left pin: emitter to common GND
- Middle pin: base
- Right pin: collector to relay IN

Each base circuit:

```text
ESP32 GPIO --- 1k ohm --- base junction
                              |
                            15k ohm
                              |
                             GND
```

- Fan 1 relay driver GPIO32
- Fan 2 relay driver GPIO33
- Relay VCC to external 5 V
- Relay GND to common GND
- Collector to relay IN
- GPIO HIGH turns the transistor on, pulls relay IN LOW, and activates the relay
- GPIO LOW turns the relay/fan off
- Both relay stages were physically verified

## Fan power wiring

The fan current path uses the external adapter and relay screw terminals:

```text
Adapter positive --- relay COM
Relay NO ---------- fan red/positive
Fan black/negative - adapter negative/common GND
```

Both fans were tested successfully. Avoid carrying fan current through thin Dupont wiring where possible. Confirm screw-terminal strands are secure and cannot short.

## Power and shutdown safety

- USB powers the ESP32
- External 5 V powers MQ heaters, relays, and fans
- Grounds must remain common
- Normal shutdown: remove external 5 V first, then USB
- Normal cold startup: USB first, confirm ESP32 boot, then external 5 V
- To restart the ESP32 while preserving MQ heater warm-up, press RST/EN once while keeping external 5 V connected
- If a repeating boot loop occurs: remove external 5 V, remove USB, wait 10 seconds, reconnect USB, then reconnect external 5 V after normal boot
- MQ sensor metal cans becoming warm is normal; do not touch them for long or place them against combustible miniature material

## Arduino project files

Primary sketch folder:

`C:\Users\umaar\School\4B\ITT569\asapguard\AsapGuard_Azure_Test\`

Primary sketch:

`C:\Users\umaar\School\4B\ITT569\asapguard\AsapGuard_Azure_Test\AsapGuard_Azure_Test.ino`

Private configuration:

`iot_configs.h`

Never share or commit `iot_configs.h`.

Other Azure sample tabs include:

- `AzIoTSasToken.cpp/.h`
- `SerialLogger.cpp/.h`
- `readme.md`

Backup/reference predictive sketch:

`C:\Users\umaar\OneDrive\Documents\AsapGuard\AsapGuard_Predictive_Rules.ino`

A malformed duplicate-code backup exists as:

`AsapGuard_Azure_Test.malformed-backup.ino.txt`

The malformed backup is not used. It was created after predictive code was accidentally appended inside an older `loop()`.

## Current firmware

Telemetry firmware string: `0.4.0-predictive-rules`

The latest uploaded code contains the final tuned thresholds even though the firmware string was not incremented. A reboot and reset sequence counter confirmed the tuned source was uploaded.

Verified compile result:

- Flash: approximately 83%
- RAM: approximately 16%
- The LiquidCrystal_I2C architecture warning is harmless

Serial commands, with Serial Monitor set to 115200 baud and No Line Ending:

- `A`: automatic predictive control
- `0`: manual mode, both fans off
- `1`: manual mode, Fan 1 on
- `2`: manual mode, both fans on
- `N`: label NORMAL
- `H`: label HUMIDITY
- `S`: label SMOKE
- `G`: label BUTANE
- `U`: label UNLABELED

Important: sending `0`, `1`, or `2` switches to manual mode. Send `A` to restore automatic control.

## Predictive rules

- Baseline requires 20 stable samples
- Telemetry/sample interval is approximately 3 seconds
- EMA filtering is used for MQ-135 and humidity
- Five-minute projections use filtered slopes
- Stage requests require persistence to reduce noise
- Fan safety hold is approximately 15 seconds
- MQ-6 trigger is intended as an immediate Stage 2 override
- Unpowered MQ inputs are ignored using MQ-135 validity checks

Final tuned decision thresholds:

- MQ-135 direct Stage 1: delta at least `+70`
- MQ-135 direct Stage 2: delta at least `+150`
- Predictive MQ trend: current delta at least `+25`, positive slope, projected delta at least `+70`
- Humidity direct Stage 1: delta at least `+3.0%`
- Predictive humidity: current delta at least `+0.8%`, slope above `0.20%/min`, projected delta at least `+3.0%`
- MQ-6 active-low trigger: immediate Stage 2

The thresholds were raised because clean-air airflow/rebound produced a false MQ prediction at about `+36.6` with the earlier threshold.

## Verified tests

### Normal air

- Baseline completed successfully
- Example stable baseline around MQ-135 `359.2`
- Normal readings around `351-356`
- Prediction `NORMAL / STABLE`
- Both fans off

### Humidity

- Damp tissue near DHT22 raised humidity to about `89%`
- Filtered humidity delta reached approximately `+3.5%`
- State `ALERT_NOW`
- Reason `HUMIDITY_RISE`
- Fan 1 activated; Fan 2 stayed off
- Test passed

### Smoke/VOC

- Vape caused MQ-135 raw approximately `583`
- MQ delta exceeded `+150`
- States included `PREDICTED_5MIN`, `MQ135_TREND`, `MQ135_RISE`, and `MQ135_HIGH`
- The controller predicted deterioration before the direct threshold
- Fan 1 physically activated
- A short Stage 2 prediction occurred, but it did not persist for three samples; stronger/longer contamination or MQ-6 is expected to command both fans
- Test passed for predictive Stage 1 behavior

### Butane/gas

- Real lighter gas did not change MQ-6 digital output; `mq6Triggered` remained false and its DOUT LED did not illuminate
- MQ-135 did respond strongly to gas/VOCs
- After restoring `AUTO_RULES`, both fans were observed on with `fanStageCommand:2`
- This confirms the two-fan actuation path, but it does not prove the physical MQ-6 sensor is calibrated
- Recalibrate MQ-6 DOUT in the lecture room; use only very brief unlit lighter taps, never continuous release

### Room-environment limitation

The home living room regularly contains vape aerosol and many occupants. MQ-135 is non-specific and repeatedly rose when Fan 1 stopped, causing on/off cycling. This is expected from residual aerosol, VOCs, CO2, and fan-induced airflow—not temperature. The controller does not currently use temperature as a fan trigger.

Recalibrate after moving to the air-conditioned lecture room. The lecture-room baseline should be treated as the demo reference.

## LCD verification

The 20x4 LCD was visually verified. A photographed stable display showed approximately:

- `AsapGuard Starting`
- `T:30.6 H:80.7`
- `MQ:345 D:-9`
- `P5:S0 STABLE`

The live temperature, humidity, MQ value, delta, predicted stage, and state are readable. The first-row `AsapGuard Starting` text remaining after startup is cosmetic. Optional polish is to change it to `ASAPGUARD NORMAL` or another dynamic status, but this is not required for function.

## Azure resources

- Resource group: `asapguard-rg`
- IoT Hub: `asapguard-hub`
- Region: East Asia
- Tier: Free/F1
- Device ID: `asapguard-esp32-01`
- Consumer group: `asapguard-monitor`
- Storage account: `asapguarddata28206`
- Container: `telemetry`
- Storage endpoint: `asapguard-storage-endpoint`
- Route: `asapguard-telemetry-route`
- Route query: `true`
- Storage encoding: JSON
- Batch frequency: 60 seconds
- Path format resembles `{iothub}/{partition}/{YYYY}/{MM}/{DD}/{HH}/{mm}.json`

An IoT Hub owner key was exposed earlier and both keys were rotated. Never reuse or paste any old key from chat history.

## Current telemetry schema

Representative fields:

- `sequence`
- `timestampUtc`
- `rtcLocal`
- `temperatureC`
- `humidityPct`
- `mq135Raw`
- `mq135Filtered`
- `mq135Baseline`
- `mq135Delta`
- `mq135SlopePerMin`
- `mq135Projected5Min`
- `humidityFiltered`
- `humidityBaseline`
- `humidityDelta`
- `humiditySlopePerMin`
- `humidityProjected5Min`
- `mq6Triggered`
- `label`
- `baselineReady`
- `baselineSamples`
- `predictionState`
- `predictionReason`
- `predictedFanStage`
- `controlMode`
- `fanStageCommand`
- `fan1On`
- `fan2On`
- `fanControlAvailable`
- `demoMode`
- `logicVersion`
- `firmwareVersion`

## Azure status and known issue

Telemetry previously published successfully and JSON files appeared in Storage. A new file was downloaded as:

`C:\Users\umaar\Downloads\08.json`

The old Codex task's local file-reading tool repeatedly froze, so this file still needs verification in the new task.

MQTT began repeated EOF/disconnect/reconnect messages after approximately one hour of runtime. The firmware uses a 60-minute SAS token duration, so this timing likely relates to token renewal behavior. For the classroom demo, restart the ESP32 shortly before presenting and complete the demo within one hour. Confirm that Storage continues receiving records after restart.

## Shared API/dashboard plan

The IoT Hub Free tier did not allow the desired additional route. Use the existing Storage JSON output as the shared source:

```text
ESP32 -> IoT Hub -> Azure Storage JSON -> small backend/API
                                             |-> Dashboard
                                             |-> Telegram
                                             |-> History
```

Suggested API contract:

- `GET /api/latest`
- `GET /api/history?minutes=...`
- Optional WebSocket or Server-Sent Events for UI updates
- Telegram alerts based on `predictionState`, `predictionReason`, or fan stage

Storage writes in roughly 60-second batches, so this dashboard path has about one minute of latency. Fan control remains immediate on the ESP32.

Do not expose Storage keys, IoT Hub keys, or device keys in browser JavaScript. The backend should use managed identity where possible, or a short-lived read-only SAS token for a temporary classroom prototype.

## Lecture-room startup and calibration

1. Inspect every wire and screw terminal before power.
2. Place the hardware on a nonconductive surface before mounting.
3. Connect USB and verify ESP32 boot.
4. Connect external 5 V.
5. Allow MQ sensors to warm for several minutes.
6. If necessary, restart the ESP32 using RST/EN while leaving MQ power on.
7. Do not introduce any stimulus while `baselineReady:false`.
8. Wait for `baselineSamples:20` and `baselineReady:true`.
9. Confirm `controlMode:AUTO_RULES`, state `NORMAL`, and both fans off.
10. Adjust the MQ-6 potentiometer in clean air: slowly approach the DOUT transition, then back off until the DOUT LED is just off.
11. Test MQ-6 with one very brief unlit lighter-gas tap from 20-30 cm away. No flame, no enclosure, and no continuous release.

## Demonstration safety

- Never combine lighter gas with flame
- Never release butane continuously
- Do not perform gas tests in an enclosed container
- Do not spray water directly at the board
- Use a squeezed damp tissue near the DHT22
- Prefer vape or extinguished candle smoke; do not move an active flame toward electronics
- Keep hot candle wax and flame at least 50 cm away
- Stop adding stimulus as soon as the expected response occurs
- Keep exposed conductors from touching the miniature structure
- Mount MQ sensors so their warm metal cans have airflow and clearance
- Keep fans clear of loose wires
- Have a manual shutdown path: external 5 V first, then USB

## Remaining work

1. Verify `08.json` contains readable, current v0.4 telemetry and all prediction/fan fields.
2. Confirm new files continue appearing after ESP32 restart.
3. Build the shared read-only API over Azure Storage JSON.
4. Give the friend building the dashboard the API schema, not Azure secrets.
5. Optionally add Telegram alerts behind the same backend.
6. Recalibrate MQ-6 in the lecture room.
7. Safely mount LCD on the right, fans on the left, sensors on top, as requested.
8. Keep power components, bare wires, relay contacts, and warm MQ cans separated from miniature material.
9. Run a final sequence: normal -> humidity -> recovery -> smoke -> recovery -> gas or safe MQ-6 simulation -> shutdown.

## Continuation update (2026-07-21)

- Verified `C:\Users\umaar\Downloads\08.json` without exposing its raw contents. It is valid newline-delimited Azure output containing five decodable telemetry records.
- Confirmed all 31 expected prediction, fan, sensor, label, version, and baseline fields are present. Firmware is `0.4.0-predictive-rules`; the file contains sequences 1252 through 1275.
- Built a read-only backend in `api/` with `GET /api/latest`, `GET /api/history?minutes=...`, and `GET /health`.
- The backend uses an explicit 31-field response allowlist, bounded history queries, origin allowlisting, a short cache, and generic errors. Unknown fields and Azure envelope metadata are discarded.
- Azure mode uses `DefaultAzureCredential` and is intended for a backend managed identity with the `Storage Blob Data Reader` role. No account key, device key, connection string, or SAS token is accepted by the implementation.
- Local mode was tested against `08.json`: both API endpoints returned HTTP 200, latest exposed exactly 31 fields, and 60-minute history returned all five records.
- Five automated parser/API tests pass, and installed dependencies reported zero known vulnerabilities at verification time.
- Added `lecture-room-runbook.md` for the remaining hardware-only calibration, installation, and final demonstration steps.

Still requires physical/external confirmation: observe a newly timestamped Azure blob after the next ESP32 restart, calibrate the real MQ-6 module in clean lecture-room air, mount the hardware, and execute the final runbook.

## Live Azure continuation update (2026-07-21)

- Granted the signed-in project owner `Storage Blob Data Reader` at the `telemetry` container scope only. No account key or SAS token was used.
- Confirmed newer blobs existed after `08.json`, through `2026-07-20T16:42:51Z` (`2026-07-21 00:42:51` Malaysia time). This completes the post-restart Storage-continuity check.
- Validated the newest blob without retaining or displaying its raw payload: two records, all 31 public fields, firmware `0.4.0-predictive-rules`, latest sequence 1885, `baselineReady:true`, and `controlMode:AUTO_RULES`.
- Tested the API directly against live Azure Storage: latest and history returned HTTP 200, the latest response contained exactly 31 public fields, and a repeat request succeeded.
- Improved Azure mode with ETag caching and eight-request download concurrency. After the first history load, refreshes download only new or changed blobs.
- A separate Arduino CLI compile attempt was inconclusive because it exceeded the command time limit while the Arduino IDE's background CLI was already active. No firmware or connected board was changed. The known-good sketch remains the previously compiled/uploaded version.

Remaining physical steps are now: lecture-room MQ-6 calibration, miniature installation, and the final demonstration checklist. Perform any deliberate firmware upload from the existing Arduino IDE session before switching away from laptop USB power.

## Public API deployment update (2026-07-21)

- Deployed the API to Azure App Service on the LinuxFree tier.
- App: `asapguard-api-28206`; plan: `asapguard-api-plan`; runtime: Node 24 LTS.
- Public base URL: `https://asapguard-api-28206.azurewebsites.net`.
- Verified `/health`, `/api/latest`, and `/api/history?minutes=60` return HTTP 200.
- Verified CORS preflight for `http://localhost:5173`; add the final dashboard URL to `ALLOWED_ORIGINS` when known.
- Enabled a system-assigned managed identity and granted it `Storage Blob Data Reader` only at the `telemetry` container scope. No Storage or IoT secret is present in the app settings or deployment package.
- Azure configuration uses prefix `asapguard-hub/01/`, a 400-blob cache window, a 360-minute maximum history request, and a 30-second cache refresh.
- The API is public and read-only. Friends should receive only its HTTPS URL and contract, never Azure credentials or `iot_configs.h`.

## Grafana/forecast update (2026-07-21)

- Expanded the deployed public contract from 31 firmware fields to 38 total fields.
- Added `mq135Stage1EtaSeconds`, `mq135Stage2EtaSeconds`, `humidityStage1EtaSeconds`, `nextThresholdEtaSeconds`, and `nextThresholdTarget`.
- ETA is calculated from the firmware's current delta and filtered slope, limited to its five-minute projection horizon. It is zero after crossing and null when no credible rising projection exists.
- Added `telemetryAgeSeconds` and `telemetryStale`; telemetry older than 180 seconds is stale.
- Normal history now anchors to current time so overnight data is not presented as live. `anchor=latest` exists only for historical panel design.
- Final deployed API returns 38 fields and all seven automated tests pass.
- Added `grafana-telegram-handoff.md` with the exact Infinity mapping, current v0.4 alert fields, thresholds, freshness behavior, and Telegram guidance.

## Final project state

The core system is functional: sensors, LCD, RTC, Azure publishing, JSON storage, predictive rules, relays, and two fans have all worked. Normal, humidity, and predictive smoke tests passed. Two-fan control was observed. The main unresolved hardware issue is real-gas sensitivity of the MQ-6 DOUT threshold. The main remaining software/cloud work is verifying the latest Storage JSON and creating a safe shared API for the dashboard and Telegram.
