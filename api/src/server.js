import path from "node:path";
import { createApp } from "./app.js";
import { createAzureSource, createLocalSource } from "./telemetry.js";

const sourceMode = process.env.TELEMETRY_SOURCE ?? "local";
const port = integer("PORT", 3000, 1, 65535);
const cacheSeconds = integer("CACHE_SECONDS", 15, 0, 3600);
const maxHistoryMinutes = integer("MAX_HISTORY_MINUTES", 1440, 1, 10080);
const staleAfterSeconds = integer("STALE_AFTER_SECONDS", 180, 1, 86400);
const allowedOrigins = new Set(
  (process.env.ALLOWED_ORIGINS ?? "http://localhost:5173,http://localhost:3000")
    .split(",").map((value) => value.trim()).filter(Boolean)
);

let source;
if (sourceMode === "azure") {
  const account = required("AZURE_STORAGE_ACCOUNT");
  const container = required("AZURE_STORAGE_CONTAINER");
  source = await createAzureSource({
    account,
    container,
    prefix: process.env.STORAGE_BLOB_PREFIX ?? "",
    maxBlobs: integer("MAX_AZURE_BLOBS", 1500, 1, 10000)
  });
} else if (sourceMode === "local") {
  const target = process.env.LOCAL_TELEMETRY_PATH ?? "../data/raw/azure-export-2026-07-20";
  source = createLocalSource(path.resolve(process.cwd(), target));
} else {
  throw new Error("TELEMETRY_SOURCE must be 'local' or 'azure'");
}

const app = createApp({ source, allowedOrigins, cacheSeconds, maxHistoryMinutes, staleAfterSeconds });
app.listen(port, () => console.log(`AsapGuard telemetry API listening on port ${port} (${source.name})`));

function required(name) {
  const value = process.env[name];
  if (!value) throw new Error(`${name} is required`);
  return value;
}

function integer(name, fallback, minimum, maximum) {
  const value = Number(process.env[name] ?? fallback);
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`);
  }
  return value;
}
