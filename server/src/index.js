// Cloudflare Worker + Durable Objects front end for the relay. The rules live in room.js.
//
//   GET  /rooms?v=N        public rooms (JSON)
//   WS   /host?name=&v=    create a room with a fresh code and join it as host
//   WS   /join/CODE?name=&v=

import { RoomCore, Listing, newCode, normalizeCode } from "./room.js";

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
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response(INFO, { headers: { "content-type": "text/plain" } });
    }

    const params = url.searchParams.toString();
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
      peers: () => this.ctx.getWebSockets().map((ws) => this.peer(ws)),
      save: (p) => p.ws.serializeAttachment(p.info),
      publish: (entry) => post("publish", entry),
      unpublish: (c) => post("unpublish", { code: c }),
    });
  }

  codeOf(ws) {
    return this.peer(ws).info.code || "";
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    const create = url.pathname === "/host";
    const open = this.ctx.getWebSockets().filter((ws) => this.peer(ws).info.id);
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
