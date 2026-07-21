# AsapGuard Grafana and Telegram handoff

## API URLs

Production dashboard:

```text
https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60
```

Alert query (fresh three-minute window):

```text
https://asapguard-api-28206.azurewebsites.net/api/history?minutes=3
```

Historical panel design while the ESP32 is off (never use for alerts):

```text
https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60&anchor=latest
```

Latest record and API health:

```text
https://asapguard-api-28206.azurewebsites.net/api/latest
https://asapguard-api-28206.azurewebsites.net/health
```

## Infinity query configuration

- Type: `JSON`
- Parser: `Backend -> JSONata` (required for Grafana Alerting)
- Source: `URL`
- Root selector: `$.data`
- Format: `Time series` for charts; `Table` or `Data frame` for state text
- Time column: `timestampUtc`, type `Time (UNIX s)`
- Authentication: none

Do not use a frontend/default parser for alert rules. The Infinity plugin requires a backend JSONata or JQ parser for Grafana Alerting.

## Current v0.4 fields to map

Sensor charts (Number):

- `temperatureC`
- `humidityPct`, `humidityFiltered`, `humidityBaseline`, `humidityDelta`
- `mq135Raw`, `mq135Filtered`, `mq135Baseline`, `mq135Delta`
- `mq135SlopePerMin`, `humiditySlopePerMin`

Controller status:

- `predictionState` (String)
- `predictionReason` (String)
- `predictedFanStage` (Number: 0, 1, or 2)
- `fanStageCommand` (Number: 0, 1, or 2)
- `fan1On`, `fan2On`, `mq6Triggered`, `baselineReady` (Boolean)
- `controlMode` (String; should be `AUTO_RULES`)

Forecast countdowns:

- `mq135Stage1EtaSeconds`
- `mq135Stage2EtaSeconds`
- `humidityStage1EtaSeconds`
- `nextThresholdEtaSeconds`
- `nextThresholdTarget`

Freshness:

- `telemetryAgeSeconds`
- `telemetryStale`

ETA values are linear rule-based estimates. They are `0` when crossed and `null` when conditions are not rising or the estimate is beyond five minutes.

## Correct alert logic

Do not reuse old absolute MQ-135 or humidity thresholds. Baselines vary by room.

| Alert | Field and condition | Meaning |
|---|---|---|
| Stage 1 | Last `fanStageCommand >= 1` | Fan 1/controller alert active |
| Stage 2 | Last `fanStageCommand >= 2` | Both fans/critical controller alert active |
| Predictive warning | Last `predictedFanStage >= 1` | Controller predicts deterioration |
| MQ-6 gas | Last `mq6Triggered == true` | MQ-6 digital gas trigger |
| Approaching threshold | `nextThresholdEtaSeconds <= 60` and not null | Threshold estimated within one minute |

Use the three-minute alert URL, evaluate every 30–60 seconds, reduce each numeric query using `Last`, and configure No Data as normal when the prototype is intentionally powered off. Storage batching means a 10-second evaluation does not produce 10-second telemetry.

The firmware rules are the source of truth:

- MQ-135 Stage 1 delta: +70
- MQ-135 Stage 2 delta: +150
- Humidity Stage 1 delta: +3.0%
- MQ-6 active-low trigger: immediate Stage 2
- Temperature is displayed but is not a fan trigger

## Telegram

Keep Telegram in Grafana Alertmanager; do not put the bot token in the API, dashboard JSON, or frontend JavaScript. Route Stage 1, Stage 2, predictive, and MQ-6 alert rules to the Telegram contact point. Include controller state, reason, stage, ETA, and timestamp in the notification. Test notifications using Grafana's contact-point test before relying on sensor stimuli.

This path is monitoring only and is delayed by Azure Storage batching. The ESP32 remains the immediate fan-control system.
