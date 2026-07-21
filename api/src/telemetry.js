import { readdir, readFile, stat } from "node:fs/promises";
import path from "node:path";

export const PUBLIC_FIELDS = Object.freeze([
  "sequence", "timestampUtc", "rtcLocal", "temperatureC", "humidityPct",
  "mq135Raw", "mq135Filtered", "mq135Baseline", "mq135Delta",
  "mq135SlopePerMin", "mq135Projected5Min", "humidityFiltered",
  "humidityBaseline", "humidityDelta", "humiditySlopePerMin",
  "humidityProjected5Min", "mq6Triggered", "label", "baselineReady",
  "baselineSamples", "predictionState", "predictionReason",
  "predictedFanStage", "controlMode", "fanStageCommand", "fan1On", "fan2On",
  "fanControlAvailable", "demoMode", "logicVersion", "firmwareVersion",
  "mq135Stage1EtaSeconds", "mq135Stage2EtaSeconds",
  "humidityStage1EtaSeconds", "nextThresholdEtaSeconds", "nextThresholdTarget",
  "telemetryAgeSeconds", "telemetryStale"
]);

const PUBLIC_FIELD_SET = new Set(PUBLIC_FIELDS);
const MAX_PROJECTION_SECONDS = 300;

function decodeBody(body) {
  if (body && typeof body === "object") return body;
  if (typeof body !== "string") return null;

  for (const candidate of [body, safeBase64Decode(body)]) {
    if (!candidate) continue;
    try {
      const parsed = JSON.parse(candidate);
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) return parsed;
    } catch {
      // Azure exports may contain either plain JSON or base64-encoded JSON.
    }
  }
  return null;
}

function safeBase64Decode(value) {
  try {
    return Buffer.from(value, "base64").toString("utf8");
  } catch {
    return null;
  }
}

export function parseAzureJson(text) {
  const trimmed = text.trim();
  if (!trimmed) return [];

  let records;
  try {
    const parsed = JSON.parse(trimmed);
    records = Array.isArray(parsed) ? parsed : [parsed];
  } catch {
    records = trimmed.split(/\r?\n/).filter(Boolean).map((line) => JSON.parse(line));
  }

  return records.flatMap((record) => {
    if (!record || typeof record !== "object" || Array.isArray(record)) return [];
    const payload = "Body" in record || "body" in record
      ? decodeBody(record.Body ?? record.body)
      : record;
    return payload ? [sanitizeTelemetry(payload)] : [];
  });
}

export function sanitizeTelemetry(record) {
  const clean = {};
  for (const [key, value] of Object.entries(record)) {
    if (PUBLIC_FIELD_SET.has(key) && isJsonValue(value)) clean[key] = value;
  }
  return addThresholdEtas(clean);
}

function addThresholdEtas(record) {
  const baselineReady = record.baselineReady === true;
  const mqStage1 = baselineReady
    ? thresholdEta(record.mq135Delta, record.mq135SlopePerMin, 70)
    : null;
  const mqStage2 = baselineReady
    ? thresholdEta(record.mq135Delta, record.mq135SlopePerMin, 150)
    : null;
  const humidityStage1 = baselineReady
    ? thresholdEta(record.humidityDelta, record.humiditySlopePerMin, 3)
    : null;

  const candidates = [
    [mqStage1, "MQ135_STAGE1"],
    [mqStage2, "MQ135_STAGE2"],
    [humidityStage1, "HUMIDITY_STAGE1"]
  ].filter(([seconds]) => seconds !== null);

  if (record.mq6Triggered === true) candidates.push([0, "MQ6_STAGE2"]);
  candidates.sort((a, b) => a[0] - b[0]);

  record.mq135Stage1EtaSeconds = mqStage1;
  record.mq135Stage2EtaSeconds = mqStage2;
  record.humidityStage1EtaSeconds = humidityStage1;
  record.nextThresholdEtaSeconds = candidates[0]?.[0] ?? null;
  record.nextThresholdTarget = candidates[0]?.[1] ?? null;
  record.telemetryAgeSeconds = null;
  record.telemetryStale = null;
  return record;
}

function thresholdEta(delta, slopePerMinute, threshold) {
  if (!Number.isFinite(delta) || !Number.isFinite(slopePerMinute)) return null;
  if (delta >= threshold) return 0;
  if (slopePerMinute <= 0) return null;

  const seconds = ((threshold - delta) / slopePerMinute) * 60;
  if (seconds < 0 || seconds > MAX_PROJECTION_SECONDS) return null;
  return Math.round(seconds);
}

function isJsonValue(value) {
  return value === null || ["string", "number", "boolean"].includes(typeof value);
}

export function timestampMillis(record) {
  const value = record.timestampUtc;
  if (typeof value === "number" && Number.isFinite(value)) {
    return value < 1e12 ? value * 1000 : value;
  }
  if (typeof value === "string") {
    if (/^\d+(\.\d+)?$/.test(value)) {
      const numeric = Number(value);
      return numeric < 1e12 ? numeric * 1000 : numeric;
    }
    const parsed = Date.parse(value);
    return Number.isNaN(parsed) ? 0 : parsed;
  }
  return 0;
}

export function normalizeRecords(records) {
  const unique = new Map();
  for (const record of records) {
    if (!record || Object.keys(record).length === 0) continue;
    const key = `${record.timestampUtc ?? ""}:${record.sequence ?? ""}`;
    unique.set(key, record);
  }
  return [...unique.values()].sort((a, b) => {
    const timeDifference = timestampMillis(a) - timestampMillis(b);
    return timeDifference || Number(a.sequence ?? 0) - Number(b.sequence ?? 0);
  });
}

async function jsonFiles(target) {
  const info = await stat(target);
  if (info.isFile()) return target.toLowerCase().endsWith(".json") ? [target] : [];

  const entries = await readdir(target, { withFileTypes: true });
  const nested = await Promise.all(entries.map((entry) => {
    const child = path.join(target, entry.name);
    if (entry.isDirectory()) return jsonFiles(child);
    return entry.isFile() && entry.name.toLowerCase().endsWith(".json") ? [child] : [];
  }));
  return nested.flat();
}

export function createLocalSource(target) {
  return {
    name: "local",
    async read() {
      const files = await jsonFiles(target);
      const batches = await Promise.all(files.map(async (file) => {
        try {
          return parseAzureJson(await readFile(file, "utf8"));
        } catch (error) {
          console.warn(`Skipping unreadable telemetry file: ${path.basename(file)} (${error.name})`);
          return [];
        }
      }));
      return normalizeRecords(batches.flat());
    }
  };
}

async function streamToText(stream) {
  if (!stream) return "";
  const chunks = [];
  for await (const chunk of stream) chunks.push(Buffer.from(chunk));
  return Buffer.concat(chunks).toString("utf8");
}

export async function createAzureSource({ account, container, prefix = "", maxBlobs = 1500 }) {
  const [{ DefaultAzureCredential }, { BlobServiceClient }] = await Promise.all([
    import("@azure/identity"),
    import("@azure/storage-blob")
  ]);
  const service = new BlobServiceClient(
    `https://${account}.blob.core.windows.net`,
    new DefaultAzureCredential()
  );
  const client = service.getContainerClient(container);
  const blobCache = new Map();

  return {
    name: "azure",
    async read() {
      const blobs = [];
      for await (const blob of client.listBlobsFlat({ prefix })) {
        if (!blob.name.toLowerCase().endsWith(".json")) continue;
        blobs.push({
          name: blob.name,
          modified: blob.properties.lastModified?.getTime() ?? 0,
          etag: blob.properties.etag ?? ""
        });
        blobs.sort((a, b) => b.modified - a.modified);
        if (blobs.length > maxBlobs) blobs.length = maxBlobs;
      }

      const selectedNames = new Set(blobs.map(({ name }) => name));
      for (const cachedName of blobCache.keys()) {
        if (!selectedNames.has(cachedName)) blobCache.delete(cachedName);
      }

      const stale = blobs.filter(({ name, etag }) => blobCache.get(name)?.etag !== etag);
      await mapWithConcurrency(stale, 8, async ({ name, etag }) => {
        const response = await client.getBlobClient(name).download();
        const records = parseAzureJson(await streamToText(response.readableStreamBody));
        blobCache.set(name, { etag, records });
      });

      return normalizeRecords(
        blobs.flatMap(({ name }) => blobCache.get(name)?.records ?? [])
      );
    }
  };
}

async function mapWithConcurrency(items, concurrency, worker) {
  let next = 0;
  const runners = Array.from({ length: Math.min(concurrency, items.length) }, async () => {
    while (next < items.length) {
      const item = items[next++];
      await worker(item);
    }
  });
  await Promise.all(runners);
}
