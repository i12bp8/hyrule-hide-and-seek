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
import { WebSocketServer } from "ws";
import { RoomCore, Listing, newCode, normalizeCode } from "./src/room.js";

const DEFAULT_LIMITS = {
  maxConnections: 400, // everyone connected at once
  maxPerIp: 12, // connections from one address (a LAN party is fine, a flood isn't)
  maxRooms: 300,
  roomsPerIpPerMinute: 10,
  maxPayload: 16 * 1024,
  pingMs: 30_000, // drop connections that stop answering
};

export function startServer({ port = 8787, host = "127.0.0.1", trustProxy = false, limits = {}, log = console.log } = {}) {
  const L = { ...DEFAULT_LIMITS, ...limits };
  const listing = new Listing();
  const rooms = new Map(); // code -> { peers: Set, core }
  const perIp = new Map(); // address -> open connections
  const creates = new Map(); // address -> recent room creation times
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

  const server = http.createServer((req, res) => {
    const url = new URL(req.url, "http://localhost");
    const headers = { "cache-control": "no-store", "x-content-type-options": "nosniff" };
    if (req.method === "GET" && url.pathname === "/rooms") {
      const list = listing.list(Number(url.searchParams.get("v")) || 0);
      res.writeHead(200, { ...headers, "content-type": "application/json", "access-control-allow-origin": "*" });
      res.end(JSON.stringify({ rooms: list }));
      return;
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
  server.requestTimeout = 10_000;

  const wss = new WebSocketServer({ noServer: true, maxPayload: L.maxPayload });

  server.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url, "http://localhost");
    const ip = addressOf(req);
    if (open >= L.maxConnections) return refuse(socket, 503, "Server Full");
    if ((perIp.get(ip) || 0) >= L.maxPerIp) return refuse(socket, 429, "Too Many Connections");

    let code = "";
    let create = false;
    if (url.pathname === "/host") {
      const now = Date.now();
      const recent = (creates.get(ip) || []).filter((t) => now - t < 60_000);
      if (recent.length >= L.roomsPerIpPerMinute || rooms.size >= L.maxRooms) {
        return refuse(socket, 429, "Too Many Rooms");
      }
      recent.push(now);
      creates.set(ip, recent);
      create = true;
      do code = newCode(); while (rooms.has(code) && rooms.get(code).peers.size > 0);
    } else {
      const m = url.pathname.match(/^\/join\/([A-Za-z]+)$/);
      code = m ? normalizeCode(m[1]) : "";
    }
    if (!code) return refuse(socket, 404, "Not Found");

    wss.handleUpgrade(req, socket, head, (ws) => {
      open += 1;
      perIp.set(ip, (perIp.get(ip) || 0) + 1);
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
    for (const [ip, times] of creates) {
      if (times.every((t) => now - t > 60_000)) creates.delete(ip);
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
