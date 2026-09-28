// Cloudflare Worker + Durable Objects front end for the relay. The rules live in room.js.
//
//   GET  /rooms?v=N                public rooms (JSON)
//   WS   /host?name=&v=            create a room with a fresh code and join it as host
//   WS   /join/CODE?name=&v=
//   POST /session/host?name=&v=    HTTP fallback for Linux Dusklight 2.0.2
//   POST /session/join/CODE?name=&v=
//   GET  /session/CODE.token/poll  long poll; POST .../send and .../leave

import {
  RoomCore,
  Listing,
  HttpPeer,
  decodeHttpBatch,
  newCode,
  normalizeCode,
} from "./room.js";

const INFO = "Hyrule Hide & Seek relay. Install the mod in Dusklight to play.\n";

function lobbyStub(env) {
  return env.LOBBY.get(env.LOBBY.idFromName("lobby"));
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/rooms") {
      return lobbyStub(env).fetch(new Request(`https://lobby/list${url.search}`));
    }
    const params = url.searchParams.toString();
    if (request.method === "POST" && url.pathname === "/session/host") {
      for (let attempt = 0; attempt < 8; ++attempt) {
        const code = newCode(() => crypto.getRandomValues(new Uint32Array(1))[0] / 2 ** 32);
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const response = await room.fetch(
          new Request(`https://room/http/host?code=${code}&${params}`, request),
        );
        if (response.status !== 409) return response;
      }
      return new Response("No free room code, try again", { status: 503 });
    }
    const httpJoin = url.pathname.match(/^\/session\/join\/([A-Za-z]+)$/);
    if (request.method === "POST" && httpJoin) {
      const code = normalizeCode(httpJoin[1]);
      if (!code) return new Response("That is not a room code", { status: 400 });
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(new Request(`https://room/http/join?code=${code}&${params}`, request));
    }
    const session = url.pathname.match(
      /^\/session\/([A-Za-z]{5})\.([a-fA-F0-9]{32})\/(poll|send|leave)$/,
    );
    if (session) {
      const code = normalizeCode(session[1]);
      const token = session[2].toLowerCase();
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(
        new Request(`https://room/http/${token}/${session[3]}?code=${code}`, request),
      );
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response(INFO, { headers: { "content-type": "text/plain" } });
    }

    if (url.pathname === "/host") {
      for (let attempt = 0; attempt < 8; ++attempt) {
        const code = newCode(() => crypto.getRandomValues(new Uint32Array(1))[0] / 2 ** 32);
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const response = await room.fetch(
          new Request(`https://room/host?code=${code}&${params}`, request),
        );
        if (response.status !== 409) return response;
      }
      return new Response("No free room code, try again", { status: 503 });
    }

    const join = url.pathname.match(/^\/join\/([A-Za-z]+)$/);
    if (join) {
      const code = normalizeCode(join[1]);
      if (!code) return new Response("That is not a room code", { status: 400 });
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(new Request(`https://room/join?code=${code}&${params}`, request));
    }

    return new Response("Not found", { status: 404 });
  },
};

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    this.cache = new Map(); // WebSocket -> peer, rebuilt after hibernation
    this.httpPeers = new Map(); // token -> { peer, code, detached }; active while long polling
  }

  peer(ws) {
    let p = this.cache.get(ws);
    if (!p) {
      p = {
        ws,
        info: ws.deserializeAttachment() || {},
        send: (data) => ws.send(data),
        close: (code, reason) => ws.close(code, reason),
      };
      this.cache.set(ws, p);
    }
    return p;
  }

  core(code) {
    const lobby = lobbyStub(this.env);
    const post = (path, body) =>
      lobby
        .fetch(new Request(`https://lobby/${path}`, { method: "POST", body: JSON.stringify(body) }))
        .catch(() => {});
    return new RoomCore(code, {
      peers: () => [
        ...this.ctx.getWebSockets().map((ws) => this.peer(ws)),
        ...[...this.httpPeers.values()].filter((entry) => !entry.detached).map((entry) => entry.peer),
      ],
      save: (p) => {
        if (p.ws) p.ws.serializeAttachment(p.info);
      },
      publish: (entry) => post("publish", entry),
      unpublish: (c) => post("unpublish", { code: c }),
    });
  }

  detachHttp(entry) {
    if (entry.detached) return;
    entry.detached = true;
    entry.peer.detached = true;
    this.core(entry.code).leave(entry.peer);
  }

  cleanupHttpPeers() {
    const now = Date.now();
    for (const [token, entry] of this.httpPeers) {
      if (now - entry.peer.lastSeen > 30_000) {
        entry.peer.close();
        this.httpPeers.delete(token);
      }
    }
  }

  openHttp(code, create, url) {
    const token = crypto.randomUUID().replaceAll("-", "");
    let entry;
    const peer = new HttpPeer(() => this.detachHttp(entry));
    entry = { token, code, peer, detached: false };
    peer.info = { code };
    this.httpPeers.set(token, entry);
    const ok = this.core(code).join(peer, {
      name: url.searchParams.get("name"),
      v: url.searchParams.get("v"),
      create,
    });
    return new Response(JSON.stringify({ session: `${code}.${token}` }), {
      status: ok || peer.closed ? 200 : 500,
      headers: { "content-type": "application/json", "cache-control": "no-store" },
    });
  }

  async http(request, url, code) {
    this.cleanupHttpPeers();
    const create = url.pathname === "/http/host";
    if (create || url.pathname === "/http/join") {
      const open = [
        ...this.ctx.getWebSockets().map((ws) => this.peer(ws)),
        ...[...this.httpPeers.values()].filter((entry) => !entry.detached).map((entry) => entry.peer),
      ].filter((peer) => peer.info.id);
      if (create && open.length > 0) return new Response("Room taken", { status: 409 });
      return this.openHttp(code, create, url);
    }

    const action = url.pathname.match(/^\/http\/([a-f0-9]{32})\/(poll|send|leave)$/);
    if (!action) return new Response("Not found", { status: 404 });
    const entry = this.httpPeers.get(action[1]);
    if (!entry || entry.code !== code) return new Response("Session ended", { status: 404 });
    const { peer } = entry;
    if (action[2] === "poll" && (request.method === "GET" || request.method === "POST")) {
      const body = await peer.poll();
      if (peer.closed && peer.queue.length === 0) this.httpPeers.delete(action[1]);
      return new Response(body, {
        headers: { "content-type": "application/octet-stream", "cache-control": "no-store" },
      });
    }
    if (action[2] === "send" && request.method === "POST") {
      if (peer.closed) return new Response("Session ended", { status: 410 });
      const length = Number(request.headers.get("content-length")) || 0;
      if (length > 16 * 1024) return new Response("Batch too large", { status: 413 });
      try {
        const body = new Uint8Array(await request.arrayBuffer());
        if (body.length > 16 * 1024) return new Response("Batch too large", { status: 413 });
        for (const message of decodeHttpBatch(body)) {
          if (peer.closed) break;
          this.core(code).message(peer, message);
        }
      } catch {
        return new Response("Invalid relay batch", { status: 400 });
      }
      peer.touch();
      return new Response(null, { status: 204 });
    }
    if (action[2] === "leave" && request.method === "POST") {
      this.detachHttp(entry);
      this.httpPeers.delete(action[1]);
      return new Response(null, { status: 204 });
    }
    return new Response("Method not allowed", { status: 405 });
  }

  codeOf(ws) {
    return this.peer(ws).info.code || "";
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    if (url.pathname.startsWith("/http/")) return this.http(request, url, code);
    const create = url.pathname === "/host";
    const open = [
      ...this.ctx.getWebSockets().map((ws) => this.peer(ws)),
      ...[...this.httpPeers.values()].filter((entry) => !entry.detached).map((entry) => entry.peer),
    ].filter((peer) => peer.info.id);
    if (create && open.length > 0) return new Response("Room taken", { status: 409 });

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    const p = this.peer(server);
    p.info = { code };
    server.serializeAttachment(p.info);
    this.core(code).join(p, {
      name: url.searchParams.get("name"),
      v: url.searchParams.get("v"),
      create,
    });
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, message) {
    this.core(this.codeOf(ws)).message(this.peer(ws), message);
  }

  async webSocketClose(ws, code) {
    const p = this.peer(ws);
    this.core(this.codeOf(ws)).leave(p);
    this.cache.delete(ws);
    try {
      ws.close(code === 1005 ? 1000 : code);
    } catch {
      // already closed
    }
  }

  async webSocketError(ws) {
    await this.webSocketClose(ws, 1011);
  }
}

export class Lobby {
  constructor() {
    this.listing = new Listing();
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === "/publish") {
      this.listing.publish(await request.json());
      return new Response("ok");
    }
    if (url.pathname === "/unpublish") {
      this.listing.unpublish((await request.json()).code);
      return new Response("ok");
    }
    const rooms = this.listing.list(Number(url.searchParams.get("v")) || 0);
    return new Response(JSON.stringify({ rooms }), {
      headers: { "content-type": "application/json", "access-control-allow-origin": "*" },
    });
  }
}
