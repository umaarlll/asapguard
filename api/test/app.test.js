import assert from "node:assert/strict";
import test from "node:test";
import { createApp } from "../src/app.js";

async function withServer(source, run) {
  const server = createApp({
    source,
    allowedOrigins: new Set(["https://dashboard.example"]),
    cacheSeconds: 0,
    maxHistoryMinutes: 120
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address();
  try {
    await run(`http://127.0.0.1:${port}`);
  } finally {
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  }
}

const source = {
  name: "test",
  async read() {
    return [
      { sequence: 1, timestampUtc: 1784560000, predictionState: "NORMAL" },
      { sequence: 2, timestampUtc: 1784563600, predictionState: "ALERT_NOW" }
    ];
  }
};

test("root describes the available API endpoints", async () => {
  await withServer(source, async (baseUrl) => {
    const response = await fetch(baseUrl);
    const body = await response.json();
    assert.equal(response.status, 200);
    assert.equal(body.status, "ok");
    assert.equal(body.endpoints.latest, "/api/latest");
  });
});

test("latest returns the newest telemetry record", async () => {
  await withServer(source, async (baseUrl) => {
    const response = await fetch(`${baseUrl}/api/latest`, {
      headers: { origin: "https://dashboard.example" }
    });
    const body = await response.json();
    assert.equal(response.status, 200);
    assert.equal(response.headers.get("access-control-allow-origin"), "https://dashboard.example");
    assert.equal(body.data.sequence, 2);
  });
});

test("history defaults to current time and supports explicit historical anchoring", async () => {
  await withServer(source, async (baseUrl) => {
    const current = await fetch(`${baseUrl}/api/history?minutes=30`);
    const currentBody = await current.json();
    assert.equal(current.status, 200);
    assert.deepEqual(currentBody.data, []);

    const valid = await fetch(`${baseUrl}/api/history?minutes=30&anchor=latest`);
    const validBody = await valid.json();
    assert.equal(valid.status, 200);
    assert.deepEqual(validBody.data.map((record) => record.sequence), [2]);

    const invalid = await fetch(`${baseUrl}/api/history?minutes=121`);
    assert.equal(invalid.status, 400);
  });
});
