# AsapGuard telemetry API

Read-only API for the dashboard and optional Telegram worker. It reads the existing Azure Storage JSON route and exposes only an explicit allowlist of the 31 telemetry fields. Azure envelope metadata and unexpected fields are discarded.

## Deployed classroom API

- Base URL: `https://asapguard-api-28206.azurewebsites.net`
- Latest: `https://asapguard-api-28206.azurewebsites.net/api/latest`
- History: `https://asapguard-api-28206.azurewebsites.net/api/history?minutes=60`
- Health: `https://asapguard-api-28206.azurewebsites.net/health`

The API is intentionally read-only but publicly reachable. Browser access is currently allowed from the configured localhost development origins. Add the exact deployed dashboard origin to `ALLOWED_ORIGINS` before publishing the dashboard.

## Endpoints

- `GET /health`
- `GET /api/latest`
- `GET /api/history?minutes=60` (maximum 1,440 by default)

Responses use `{ "data": ..., "meta": ... }`. Storage batches arrive about once per minute; the ESP32 fan controller remains the immediate safety/control path.

Each telemetry row includes five API-derived forecast fields:

- `mq135Stage1EtaSeconds` — estimated seconds until MQ-135 delta reaches +70.
- `mq135Stage2EtaSeconds` — estimated seconds until MQ-135 delta reaches +150.
- `humidityStage1EtaSeconds` — estimated seconds until humidity delta reaches +3%.
- `nextThresholdEtaSeconds` and `nextThresholdTarget` — nearest currently projected rule threshold.

The estimate is linear from the firmware's filtered slope. It is `0` when crossed and `null` when the baseline is not ready, the slope is not rising, or the estimate is beyond the firmware's five-minute projection horizon. It is a classroom forecast, not a safety guarantee.

`telemetryAgeSeconds` and `telemetryStale` protect dashboards and alerts from treating old Storage records as live. History is anchored to the current time by default. For dashboard design with a powered-off device, add `&anchor=latest`; never use that historical mode for active alerts.

## Run locally with the downloaded verification file

From PowerShell in this directory:

```powershell
$env:TELEMETRY_SOURCE='local'
$env:LOCAL_TELEMETRY_PATH='C:\Users\umaar\Downloads\08.json'
npm install
npm test
npm start
```

Then open `http://localhost:3000/api/latest`. Do not put Azure credentials in this repository, dashboard JavaScript, URLs, or API responses.

## Azure deployment configuration

Set these App Service environment variables:

```text
TELEMETRY_SOURCE=azure
AZURE_STORAGE_ACCOUNT=<account name only>
AZURE_STORAGE_CONTAINER=telemetry
STORAGE_BLOB_PREFIX=<optional path prefix>
MAX_AZURE_BLOBS=1500
ALLOWED_ORIGINS=https://your-dashboard.example
```

Enable a system-assigned managed identity on the backend and grant it **Storage Blob Data Reader** at the narrowest practical scope (preferably the telemetry container). `DefaultAzureCredential` then authenticates without a storage key. The API never accepts a connection string, account key, or SAS token.

Azure blob contents are cached by ETag. After the first history load, each refresh downloads only new or changed blobs. Set `STORAGE_BLOB_PREFIX` to the device route prefix and size `MAX_AZURE_BLOBS` for the history window you need.

For local Azure development, sign in with Azure CLI (`az login`) and give that identity read access. For a classroom-only offline demo, keep `TELEMETRY_SOURCE=local` and point it at downloaded JSON.

## Dashboard contract

Treat telemetry as delayed historical/monitoring data, not a real-time safety signal. Poll `/api/latest` every 15–30 seconds. Use `predictionState`, `predictionReason`, `fanStageCommand`, and `mq6Triggered` for status and alert presentation. Use `/api/history` for charts.

Telegram alerts should run server-side and consume the same API or source. Deduplicate alerts by `sequence`, trigger on a transition into an alert state, and never place a bot token in frontend code.
