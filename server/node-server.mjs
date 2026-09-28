// The relay on plain Node.js: for testing on your own computer and for hosting it yourself.
//
//   npm install
//   node node-server.mjs [--port 8787] [--host 127.0.0.1] [--trust-proxy]
//
// The game only allows plain ws:// to localhost. To host for other people, put this behind
// something that adds HTTPS (Tailscale Funnel, Cloudflare Tunnel, Caddy) and use wss://; see
// deploy/README.md. Pass --trust-proxy then, so limits apply per player instead of per proxy.
//
// Built for the open internet: it serves no files, stores nothing, doesn't log addresses, and caps
// connections, rooms and message rates so one person can't take it down for everyone.

import http from "node:http";
import { randomBytes } from "node:crypto";
import { WebSocketServer } from "ws";
import {
  RoomCore,
  Listing,
  HttpPeer,
  decodeHttpBatch,
  newCode,
  normalizeCode,
} from "./src/room.js";

const DEFAULT_LIMITS = {
  maxConnections: 400, // everyone connected at once
  maxPerIp: 32, // two full rooms can share one home, school or LAN address
  maxRooms: 300,
  roomsPerIpPerMinute: 10,
  sessionsPerIpPerMinute: 60,
  maxPayload: 16 * 1024,
  pingMs: 30_000, // drop connections that stop answering
};

export function startServer({ port = 8787, host = "127.0.0.1", trustProxy = false, limits = {}, log = console.log } = {}) {
  const L = { ...DEFAULT_LIMITS, ...limits };
  const listing = new Listing();
  const rooms = new Map(); // code -> { peers: Set, core }
  const sessions = new Map(); // CODE.token -> HTTP long-poll peer
  const perIp = new Map(); // address -> open connections
  const creates = new Map(); // address -> recent room creation times
  const sessionOpens = new Map(); // address -> recent HTTP fallback sessions
  let open = 0;

  function room(code) {
    let r = rooms.get(code);
    if (!r) {
      const peers = new Set();
      r = {
        peers,
        core: new RoomCore(code, {
          peers: () => [...peers],
          save: () => {},
          publish: (entry) => listing.publish(entry),
          unpublish: (c) => listing.unpublish(c),
        }),
      };
      rooms.set(code, r);
    }
    return r;
  }

  // Behind a proxy every connection comes from the proxy; the proxy appends the real address as
  // the last X-Forwarded-For entry (earlier entries are whatever the client claimed).
  function addressOf(req) {
    if (trustProxy) {
      const xff = String(req.headers["x-forwarded-for"] || "").split(",").map((s) => s.trim()).filter(Boolean);
      if (xff.length > 0) return xff[xff.length - 1];
    }
    return req.socket.remoteAddress || "?";
  }

  function refuse(socket, status, text) {
    socket.write(`HTTP/1.1 ${status} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
    socket.destroy();
  }

  function admission(ip, create, wantedCode = "") {
    if (open >= L.maxConnections) return { status: 503, text: "Server Full" };
    if ((perIp.get(ip) || 0) >= L.maxPerIp) return { status: 429, text: "Too Many Connections" };
    if (!create) {
      const code = normalizeCode(wantedCode);
      return code ? { code } : { status: 404, text: "Not Found" };
    }
    const now = Date.now();
    const recent = (creates.get(ip) || []).filter((t) => now - t < 60_000);
    if (recent.length >= L.roomsPerIpPerMinute || rooms.size >= L.maxRooms) {
      return { status: 429, text: "Too Many Rooms" };
    }
    recent.push(now);
    creates.set(ip, recent);
    let code;
    do code = newCode(); while (rooms.has(code) && rooms.get(code).peers.size > 0);
    return { code };
  }

  function allowHttpSession(ip) {
    const now = Date.now();
    const recent = (sessionOpens.get(ip) || []).filter((time) => now - time < 60_000);
    if (recent.length >= L.sessionsPerIpPerMinute) return false;
    recent.push(now);
    sessionOpens.set(ip, recent);
    return true;
  }

  function attach(ip) {
    open += 1;
    perIp.set(ip, (perIp.get(ip) || 0) + 1);
  }

  function detach(entry) {
    if (entry.detached) return;
    entry.detached = true;
    entry.peer.detached = true;
    entry.room.core.leave(entry.peer);
    entry.room.peers.delete(entry.peer);
    open -= 1;
    const n = (perIp.get(entry.ip) || 1) - 1;
    if (n <= 0) perIp.delete(entry.ip);
    else perIp.set(entry.ip, n);
    if (entry.room.peers.size === 0) rooms.delete(entry.code);
  }

  function openHttpSession(ip, code, create, name, version) {
    const token = randomBytes(16).toString("hex");
    const session = `${code}.${token}`;
    const r = room(code);
    let entry;
    const peer = new HttpPeer(() => detach(entry));
    entry = { session, code, ip, room: r, peer, detached: false };
    peer.info = { code };
    sessions.set(session, entry);
    r.peers.add(peer);
    attach(ip);
    const ok = r.core.join(peer, { name, v: version, create });
    if (ok) log(`room ${code}: player ${peer.info.id} joined over HTTP (${r.peers.size} in room, ${open} online)`);
    return session;
  }

  async function readBody(req) {
    const chunks = [];
    let size = 0;
    for await (const chunk of req) {
      size += chunk.length;
      if (size > L.maxPayload) throw new Error("request too large");
      chunks.push(chunk);
    }
    return new Uint8Array(Buffer.concat(chunks));
  }

  function response(res, status, headers, body = undefined) {
    res.writeHead(status, { "cache-control": "no-store", "x-content-type-options": "nosniff", ...headers });
    res.end(body);
  }

  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, "http://localhost");
    const headers = { "cache-control": "no-store", "x-content-type-options": "nosniff" };
    if (req.method === "GET" && url.pathname === "/rooms") {
      const list = listing.list(Number(url.searchParams.get("v")) || 0);
      res.writeHead(200, { ...headers, "content-type": "application/json", "access-control-allow-origin": "*" });
      res.end(JSON.stringify({ rooms: list }));
      return;
    }
    const opening =
      url.pathname === "/session/host"
        ? { create: true, wanted: "" }
        : (() => {
            const match = url.pathname.match(/^\/session\/join\/([A-Za-z]+)$/);
            return match ? { create: false, wanted: match[1] } : null;
          })();
    if (req.method === "POST" && opening) {
      const ip = addressOf(req);
      if (!allowHttpSession(ip)) return response(res, 429, {}, "Too Many Sessions");
      const admitted = admission(ip, opening.create, opening.wanted);
      if (!admitted.code) return response(res, admitted.status, {}, admitted.text);
      const session = openHttpSession(
        ip,
        admitted.code,
        opening.create,
        url.searchParams.get("name"),
        url.searchParams.get("v"),
      );
      return response(
        res,
        200,
        { "content-type": "application/json" },
        JSON.stringify({ session }),
      );
    }
    const action = url.pathname.match(/^\/session\/([A-Z]{5}\.[a-f0-9]{32})\/(poll|send|leave)$/i);
    if (action) {
      const key = `${action[1].slice(0, 5).toUpperCase()}.${action[1].slice(6).toLowerCase()}`;
      const entry = sessions.get(key);
      if (!entry) return response(res, 404, {}, "Session ended");
      const { peer } = entry;
      if (action[2] === "poll" && (req.method === "GET" || req.method === "POST")) {
        const batch = await peer.poll();
        if (peer.closed && peer.queue.length === 0) sessions.delete(entry.session);
        return response(res, 200, { "content-type": "application/octet-stream" }, Buffer.from(batch));
      }
      if (action[2] === "send" && req.method === "POST") {
        if (peer.closed) return response(res, 410, {}, "Session ended");
        try {
          for (const message of decodeHttpBatch(await readBody(req))) {
            if (peer.closed) break;
            entry.room.core.message(peer, message);
          }
        } catch {
          return response(res, 400, {}, "Invalid relay batch");
        }
        peer.touch();
        return response(res, 204, {});
      }
      if (action[2] === "leave" && req.method === "POST") {
        detach(entry);
        sessions.delete(entry.session);
        return response(res, 204, {});
      }
      return response(res, 405, { allow: action[2] === "poll" ? "GET, POST" : "POST" });
    }
    if (req.method === "GET" && url.pathname === "/") {
      res.writeHead(200, { ...headers, "content-type": "text/plain" });
      res.end("Hyrule Hide & Seek relay. Install the mod in Dusklight to play.\n");
      return;
    }
    res.writeHead(404, headers);
    res.end();
  });
  server.headersTimeout = 10_000;
  server.requestTimeout = 20_000;

  const wss = new WebSocketServer({ noServer: true, maxPayload: L.maxPayload });

  server.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url, "http://localhost");
    const ip = addressOf(req);
    const create = url.pathname === "/host";
    const match = url.pathname.match(/^\/join\/([A-Za-z]+)$/);
    if (!create && !match) return refuse(socket, 404, "Not Found");
    const admitted = admission(ip, create, match ? match[1] : "");
    if (!admitted.code) return refuse(socket, admitted.status, admitted.text);
    const code = admitted.code;

    wss.handleUpgrade(req, socket, head, (ws) => {
      attach(ip);
      const r = room(code);
      const peer = {
        info: { code },
        send: (data) => ws.send(data),
        close: (c, reason) => ws.close(c, reason),
      };
      r.peers.add(peer);
      ws.alive = true;
      ws.on("pong", () => (ws.alive = true));
      ws.on("message", (data, isBinary) => {
        r.core.message(peer, isBinary ? new Uint8Array(data) : data.toString());
      });
      ws.on("error", () => {});
      ws.on("close", () => {
        open -= 1;
        const n = (perIp.get(ip) || 1) - 1;
        if (n <= 0) perIp.delete(ip);
        else perIp.set(ip, n);
        r.core.leave(peer);
        r.peers.delete(peer);
        if (r.peers.size === 0) rooms.delete(code);
      });
      const ok = r.core.join(peer, {
        name: url.searchParams.get("name"),
        v: url.searchParams.get("v"),
        create,
      });
      if (ok) log(`room ${code}: player ${peer.info.id} joined (${r.peers.size} in room, ${open} online)`);
    });
  });

  const heartbeat = setInterval(() => {
    for (const ws of wss.clients) {
      if (!ws.alive) {
        ws.terminate();
        continue;
      }
      ws.alive = false;
      ws.ping();
    }
    const now = Date.now();
    for (const [session, entry] of sessions) {
      if (now - entry.peer.lastSeen > 30_000) {
        entry.peer.close();
        sessions.delete(session);
      }
    }
    for (const [ip, times] of creates) {
      if (times.every((t) => now - t > 60_000)) creates.delete(ip);
    }
    for (const [ip, times] of sessionOpens) {
      if (times.every((time) => now - time > 60_000)) sessionOpens.delete(ip);
    }
  }, L.pingMs);

  return new Promise((resolve) => {
    server.listen(port, host, () => {
      const address = server.address();
      resolve({
        port: address.port,
        close: () =>
          new Promise((done) => {
            clearInterval(heartbeat);
            for (const entry of sessions.values()) entry.peer.close();
            sessions.clear();
            for (const client of wss.clients) client.terminate();
            server.close(done);
          }),
      });
    });
  });
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const arg = (name, fallback) => {
    const i = process.argv.indexOf(name);
    return i > 0 ? process.argv[i + 1] : fallback;
  };
  const { port } = await startServer({
    port: Number(arg("--port", 8787)),
    host: arg("--host", "127.0.0.1"),
    trustProxy: process.argv.includes("--trust-proxy"),
  });
  console.log(`relay listening on port ${port}`);
  const stop = () => process.exit(0);
  process.on("SIGTERM", stop);
  process.on("SIGINT", stop);
}
