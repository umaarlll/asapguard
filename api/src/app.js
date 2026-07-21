import http from "node:http";
import { timestampMillis } from "./telemetry.js";

function json(response, status, body, headers = {}) {
  response.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
    "x-content-type-options": "nosniff",
    "referrer-policy": "no-referrer",
    ...headers
  });
  response.end(JSON.stringify(body));
}

function corsHeaders(origin, allowedOrigins) {
  if (!origin || !allowedOrigins.has(origin)) return {};
  return {
    "access-control-allow-origin": origin,
    "access-control-allow-methods": "GET, OPTIONS",
    "access-control-allow-headers": "content-type",
    "vary": "origin"
  };
}

export function createApp({
  source,
  allowedOrigins = new Set(),
  cacheSeconds = 15,
  maxHistoryMinutes = 1440,
  staleAfterSeconds = 180
}) {
  let cache = { expires: 0, records: [] };

  async function records() {
    if (Date.now() < cache.expires) return cache.records;
    const fresh = await source.read();
    cache = { records: fresh, expires: Date.now() + cacheSeconds * 1000 };
    return fresh;
  }

  return http.createServer(async (request, response) => {
    const origin = request.headers.origin;
    const cors = corsHeaders(origin, allowedOrigins);
    if (request.method === "OPTIONS") {
      response.writeHead(origin && !allowedOrigins.has(origin) ? 403 : 204, cors);
      return response.end();
    }
    if (request.method !== "GET") return json(response, 405, { error: "method_not_allowed" }, cors);

    const url = new URL(request.url, "http://localhost");
    try {
      if (url.pathname === "/") {
        return json(response, 200, {
          name: "AsapGuard Telemetry API",
          status: "ok",
          endpoints: {
            health: "/health",
            latest: "/api/latest",
            history: "/api/history?minutes=60"
          }
        }, cors);
      }

      if (url.pathname === "/health") {
        return json(response, 200, { status: "ok", source: source.name }, cors);
      }

      const all = await records();
      if (url.pathname === "/api/latest") {
        const latest = all.at(-1);
        if (!latest) return json(response, 404, { error: "telemetry_not_found" }, cors);
        return json(response, 200, {
          data: withFreshness(latest, Date.now(), staleAfterSeconds),
          meta: { retrievedAt: new Date().toISOString(), source: source.name }
        }, cors);
      }

      if (url.pathname === "/api/history") {
        const rawMinutes = url.searchParams.get("minutes") ?? "60";
        const minutes = Number(rawMinutes);
        if (!Number.isFinite(minutes) || minutes <= 0 || minutes > maxHistoryMinutes) {
          return json(response, 400, {
            error: "invalid_minutes",
            message: `minutes must be greater than 0 and at most ${maxHistoryMinutes}`
          }, cors);
        }
        const anchor = url.searchParams.get("anchor") ?? "now";
        if (!["now", "latest"].includes(anchor)) {
          return json(response, 400, {
            error: "invalid_anchor",
            message: "anchor must be 'now' or 'latest'"
          }, cors);
        }
        const now = Date.now();
        const anchorTime = anchor === "latest" && all.length ? timestampMillis(all.at(-1)) : now;
        const cutoff = anchorTime - minutes * 60_000;
        const selected = all.filter((item) => timestampMillis(item) >= cutoff);
        return json(response, 200, {
          data: selected.map((item) => withFreshness(item, now, staleAfterSeconds)),
          meta: {
            minutes,
            anchor,
            count: selected.length,
            retrievedAt: new Date(now).toISOString(),
            source: source.name
          }
        }, cors);
      }

      return json(response, 404, { error: "not_found" }, cors);
    } catch (error) {
      console.error("Telemetry request failed:", error?.name ?? "Error");
      return json(response, 503, { error: "telemetry_unavailable" }, cors);
    }
  });
}

function withFreshness(record, now, staleAfterSeconds) {
  const observed = timestampMillis(record);
  const ageSeconds = observed ? Math.max(0, Math.floor((now - observed) / 1000)) : null;
  return {
    ...record,
    telemetryAgeSeconds: ageSeconds,
    telemetryStale: ageSeconds === null || ageSeconds > staleAfterSeconds
  };
}
