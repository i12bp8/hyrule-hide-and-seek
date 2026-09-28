// Room logic shared by the Cloudflare Worker (index.js) and the Node server (../dev-server.mjs).
//
// The relay knows nothing about hide and seek. It keeps players in rooms and forwards bytes:
//
//   client -> relay  binary  [u8 to][payload]    to: 0 = everyone else, 255 = host, 1-16 = player
//   relay  -> client binary  [u8 from][payload]
//
// Linux Dusklight 2.0.2 accidentally shipped without its WebSocket backend. The HTTP fallback uses
// the exact same messages, packed as [u8 kind][u16 little-endian length][bytes]. kind 0 is JSON
// text and kind 1 is a binary relay frame. A long poll carries server-to-client batches; POSTs
// carry client-to-server batches.
//
// Control messages are JSON text frames:
//
//   relay -> client  {op:"welcome", code, you, host, players:[{id,name}], relay}
//                    {op:"join", id, name}  {op:"leave", id}  {op:"host", id}
//                    {op:"error", why, detail}   (then the socket closes)
//                    {op:"pong"}
//   client -> relay  {op:"meta", public, label, mode, map, phase}   host only; lobby listing
//                    {op:"kick", id}                                host only
//                    {op:"ping"}
//
// A peer is { send(data), close(code, reason), info }. The store hides where peers live:
//   store.peers()        every open peer in this room
//   store.save(peer)     persist peer.info (the Worker keeps it in the socket attachment so it
//                        survives hibernation)
//   store.publish(entry) / store.unpublish(code)   update the public room list

export const RELAY_PROTOCOL = 1;
export const MAX_PLAYERS = 16;
export const MAX_MESSAGE_BYTES = 4096;
export const TO_EVERYONE = 0;
export const TO_HOST = 255;

const NAME_MAX = 20;
const LABEL_MAX = 32;
const RATE_PER_SECOND = 60;
const RATE_BURST = 120;
const HTTP_QUEUE_BYTES = 128 * 1024;

export const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ";
export const CODE_LENGTH = 5;

export function newCode(random = Math.random) {
  let out = "";
  for (let i = 0; i < CODE_LENGTH; ++i) {
    out += CODE_ALPHABET[Math.floor(random() * CODE_ALPHABET.length)];
  }
  return out;
}

export function normalizeCode(text) {
  const code = String(text || "").toUpperCase().replace(/[^A-Z]/g, "");
  return code.length === CODE_LENGTH ? code : "";
}

export function cleanName(text) {
  const name = String(text || "")
    .replace(/[\u0000-\u001f\u007f]/g, "")
    .trim()
    .slice(0, NAME_MAX);
  return name || "Player";
}

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export function encodeHttpBatch(messages) {
  const encoded = messages.map((data) => ({
    kind: typeof data === "string" ? 0 : 1,
    bytes: typeof data === "string" ? encoder.encode(data) : new Uint8Array(data),
  }));
  const size = encoded.reduce((n, item) => n + 3 + item.bytes.length, 0);
  const out = new Uint8Array(size);
  let at = 0;
  for (const item of encoded) {
    if (item.bytes.length > 0xffff) throw new Error("HTTP relay message is too large");
    out[at++] = item.kind;
    out[at++] = item.bytes.length & 0xff;
    out[at++] = item.bytes.length >> 8;
    out.set(item.bytes, at);
    at += item.bytes.length;
  }
  return out;
}

export function decodeHttpBatch(data) {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  const out = [];
  let at = 0;
  while (at < bytes.length) {
    if (at + 3 > bytes.length) throw new Error("truncated HTTP relay header");
    const kind = bytes[at++];
    const size = bytes[at++] | (bytes[at++] << 8);
    if ((kind !== 0 && kind !== 1) || size > MAX_MESSAGE_BYTES || at + size > bytes.length) {
      throw new Error("invalid HTTP relay message");
    }
    const payload = bytes.slice(at, at + size);
    at += size;
    out.push(kind === 0 ? decoder.decode(payload) : payload);
  }
  return out;
}

// A RoomCore peer backed by HTTP long polling instead of a WebSocket. The owner removes it from
// the room in onClose, but keeps the token alive until the queued error has been polled.
export class HttpPeer {
  constructor(onClose, now = () => Date.now()) {
    this.info = {};
    this.closed = false;
    this.detached = false;
    this.lastSeen = now();
    this.queue = [];
    this.queueBytes = 0;
    this.waiter = null;
    this.timer = null;
    this.onClose = onClose;
    this.now = now;
  }

  touch() {
    this.lastSeen = this.now();
  }

  send(data) {
    if (this.closed) return;
    const size = typeof data === "string" ? encoder.encode(data).length : data.byteLength;
    if (size > MAX_MESSAGE_BYTES || this.queueBytes + size + 3 > HTTP_QUEUE_BYTES) {
      this.close(4000, "queue_full");
      return;
    }
    this.queue.push(data);
    this.queueBytes += size + 3;
    this.wake();
  }

  close() {
    if (this.closed) return;
    this.closed = true;
    if (this.onClose) this.onClose();
    this.wake();
  }

  wake() {
    if (!this.waiter) return;
    const resolve = this.waiter;
    this.waiter = null;
    clearTimeout(this.timer);
    this.timer = null;
    resolve(this.drain());
  }

  drain() {
    const out = encodeHttpBatch(this.queue);
    this.queue = [];
    this.queueBytes = 0;
    return out;
  }

  poll(waitMs = 8000) {
    this.touch();
    if (this.queue.length || this.closed) return Promise.resolve(this.drain());
    // A second poll supersedes a stale one from the same client.
    if (this.waiter) this.wake();
    return new Promise((resolve) => {
      this.waiter = resolve;
      this.timer = setTimeout(() => this.wake(), waitMs);
    });
  }
}

function cleanLabel(text) {
  return String(text || "")
    .replace(/[\u0000-\u001f\u007f]/g, "")
    .trim()
    .slice(0, LABEL_MAX);
}

function sendJson(peer, obj) {
  try {
    peer.send(JSON.stringify(obj));
  } catch {
    // The socket is already gone; its close handler cleans up.
  }
}

function fail(peer, why, detail = "") {
  sendJson(peer, { op: "error", why, detail });
  try {
    peer.close(4000, why);
  } catch {
    // already closed
  }
}

// Token bucket per peer. Kept in memory only: after a hibernation it simply starts full again.
const buckets = new WeakMap();

function allow(peer, now) {
  let b = buckets.get(peer.info) || { tokens: RATE_BURST, at: now };
  b.tokens = Math.min(RATE_BURST, b.tokens + ((now - b.at) / 1000) * RATE_PER_SECOND);
  b.at = now;
  const ok = b.tokens >= 1;
  if (ok) b.tokens -= 1;
  buckets.set(peer.info, b);
  return ok;
}

export class RoomCore {
  constructor(code, store, now = () => Date.now()) {
    this.code = code;
    this.store = store;
    this.now = now;
  }

  members() {
    return this.store.peers().filter((p) => p.info && p.info.id);
  }

  hostPeer() {
    return this.members().find((p) => p.info.host) || null;
  }

  // Returns true if the peer joined. On failure the peer has been sent an error and closed.
  join(peer, { name, v, create }) {
    const members = this.members();
    const version = Number(v) || 0;
    if (create && members.length > 0) {
      fail(peer, "taken");
      return false;
    }
    if (!create && members.length === 0) {
      fail(peer, "no_room");
      return false;
    }
    if (members.length >= MAX_PLAYERS) {
      fail(peer, "full");
      return false;
    }
    const host = members.find((p) => p.info.host);
    if (host && host.info.v !== version) {
      fail(peer, "version", String(host.info.v));
      return false;
    }

    const used = new Set(members.map((p) => p.info.id));
    let id = 1;
    while (used.has(id)) ++id;

    let wanted = cleanName(name);
    const names = new Set(members.map((p) => p.info.name.toLowerCase()));
    let unique = wanted;
    for (let n = 2; names.has(unique.toLowerCase()); ++n) {
      unique = `${wanted.slice(0, NAME_MAX - 3)} ${n}`;
    }

    peer.info = {
      id,
      name: unique,
      host: members.length === 0,
      v: version,
      code: this.code,
      meta: null,
    };
    this.store.save(peer);

    const everyone = [...members, peer];
    const hostId = everyone.find((p) => p.info.host).info.id;
    sendJson(peer, {
      op: "welcome",
      code: this.code,
      you: id,
      host: hostId,
      players: everyone.map((p) => ({ id: p.info.id, name: p.info.name })),
      relay: RELAY_PROTOCOL,
    });
    for (const other of members) sendJson(other, { op: "join", id, name: unique });
    this.refreshListing();
    return true;
  }

  message(peer, data) {
    if (!peer.info || !peer.info.id) return;
    if (!allow(peer, this.now())) return; // silently drop floods

    if (typeof data === "string") {
      if (data.length > MAX_MESSAGE_BYTES) return fail(peer, "too_big");
      let msg;
      try {
        msg = JSON.parse(data);
      } catch {
        return;
      }
      return this.control(peer, msg);
    }

    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    if (bytes.length < 2 || bytes.length > MAX_MESSAGE_BYTES) return;
    const to = bytes[0];
    const out = new Uint8Array(bytes.length);
    out.set(bytes);
    out[0] = peer.info.id;

    for (const other of this.members()) {
      if (other === peer || other.info.id === peer.info.id) continue;
      const match =
        to === TO_EVERYONE || other.info.id === to || (to === TO_HOST && other.info.host);
      if (!match) continue;
      try {
        other.send(out);
      } catch {
        // closed; cleaned up by its close handler
      }
    }
  }

  control(peer, msg) {
    if (!msg || typeof msg !== "object") return;
    if (msg.op === "ping") return sendJson(peer, { op: "pong" });
    if (!peer.info.host) return;
    if (msg.op === "meta") {
      peer.info.meta = {
        public: msg.public === true,
        label: cleanLabel(msg.label),
        mode: Number(msg.mode) || 0,
        map: Number(msg.map) || 0,
        phase: Number(msg.phase) || 0,
      };
      this.store.save(peer);
      this.refreshListing();
    } else if (msg.op === "kick") {
      const target = this.members().find((p) => p.info.id === Number(msg.id) && p !== peer);
      if (target) fail(target, "kicked");
    }
  }

  leave(peer) {
    const info = peer.info;
    if (!info || !info.id) return;
    peer.info = { ...info, id: 0 };
    const rest = this.members().filter((p) => p !== peer && p.info.id !== info.id);
    for (const other of rest) sendJson(other, { op: "leave", id: info.id });

    if (info.host && rest.length > 0) {
      const next = rest.reduce((a, b) => (a.info.id < b.info.id ? a : b));
      next.info.host = true;
      next.info.meta = info.meta;
      this.store.save(next);
      for (const other of rest) sendJson(other, { op: "host", id: next.info.id });
    }
    this.refreshListing(rest);
  }

  refreshListing(members = this.members()) {
    const host = members.find((p) => p.info.host);
    if (!host || !host.info.meta || !host.info.meta.public) {
      this.store.unpublish(this.code);
      return;
    }
    const m = host.info.meta;
    this.store.publish({
      code: this.code,
      label: m.label || `${host.info.name}'s room`,
      players: members.length,
      max: MAX_PLAYERS,
      mode: m.mode,
      map: m.map,
      phase: m.phase,
      v: host.info.v,
      at: this.now(),
    });
  }
}

// The public room list. One instance for the whole server.
export class Listing {
  constructor(now = () => Date.now()) {
    this.rooms = new Map();
    this.now = now;
  }

  publish(entry) {
    this.rooms.set(entry.code, entry);
  }

  unpublish(code) {
    this.rooms.delete(code);
  }

  list(version) {
    const cutoff = this.now() - 3 * 60 * 1000;
    const out = [];
    for (const [code, e] of this.rooms) {
      if (e.at < cutoff) {
        this.rooms.delete(code);
        continue;
      }
      if (version && e.v !== version) continue;
      if (e.players >= e.max) continue;
      out.push({
        code: e.code,
        label: e.label,
        players: e.players,
        max: e.max,
        mode: e.mode,
        map: e.map,
        phase: e.phase,
      });
    }
    out.sort((a, b) => b.players - a.players || a.code.localeCompare(b.code));
    return out.slice(0, 50);
  }
}
