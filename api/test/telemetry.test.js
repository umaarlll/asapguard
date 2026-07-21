import assert from "node:assert/strict";
import test from "node:test";
import { normalizeRecords, parseAzureJson, PUBLIC_FIELDS } from "../src/telemetry.js";

const telemetry = {
  sequence: 42,
  timestampUtc: 1784563723,
  temperatureC: 24.5,
  predictionState: "NORMAL",
  firmwareVersion: "0.4.0-predictive-rules",
  secretAccidentallyAdded: "must-not-leak"
};

test("parses Azure NDJSON and decodes Body", () => {
  const body = Buffer.from(JSON.stringify(telemetry)).toString("base64");
  const text = `${JSON.stringify({ Body: body, EnqueuedTimeUtc: "ignored" })}\n`;
  const [record] = parseAzureJson(text);
  assert.equal(record.sequence, 42);
  assert.equal(record.predictionState, "NORMAL");
  assert.equal(record.secretAccidentallyAdded, undefined);
});

test("public contract contains all current firmware fields", () => {
  assert.equal(PUBLIC_FIELDS.length, 38);
  for (const field of ["mq6Triggered", "predictionReason", "fanStageCommand", "logicVersion"]) {
    assert.ok(PUBLIC_FIELDS.includes(field));
  }
});

test("estimates threshold arrival from current delta and slope", () => {
  const [record] = parseAzureJson(JSON.stringify({
    baselineReady: true,
    mq135Delta: 22,
    mq135SlopePerMin: 60,
    humidityDelta: 0,
    humiditySlopePerMin: 0
  }));
  assert.equal(record.mq135Stage1EtaSeconds, 48);
  assert.equal(record.mq135Stage2EtaSeconds, 128);
  assert.equal(record.humidityStage1EtaSeconds, null);
  assert.equal(record.nextThresholdEtaSeconds, 48);
  assert.equal(record.nextThresholdTarget, "MQ135_STAGE1");
});

test("returns zero at a crossed threshold and null outside the five-minute horizon", () => {
  const [record] = parseAzureJson(JSON.stringify({
    baselineReady: true,
    mq135Delta: 93,
    mq135SlopePerMin: 20,
    humidityDelta: 0,
    humiditySlopePerMin: 0.1
  }));
  assert.equal(record.mq135Stage1EtaSeconds, 0);
  assert.equal(record.mq135Stage2EtaSeconds, 171);
  assert.equal(record.humidityStage1EtaSeconds, null);
});

test("normalization sorts and removes duplicate sequence/timestamp pairs", () => {
  const records = normalizeRecords([
    { sequence: 2, timestampUtc: 200 },
    { sequence: 1, timestampUtc: 100 },
    { sequence: 2, timestampUtc: 200 }
  ]);
  assert.deepEqual(records.map((record) => record.sequence), [1, 2]);
});
