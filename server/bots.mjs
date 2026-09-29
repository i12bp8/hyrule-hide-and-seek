// Bot players for testing the mod on your own.
//
//   node bots.mjs --room ABCDE [--count 3] [--server ws://127.0.0.1:8787]
//
// Host a room in the game, then start the bots with its code. They stand near you, follow you
// into the round's map, and play along:
//   - as props they pick a random prop and stand still a few steps away (hunt them with B),
//     taunting now and then;
//   - as hunters they walk towards you and tag you when they get close.
// They speak the same wire format as src/protocol.hpp.

const args = Object.fromEntries(
  process.argv.slice(2).reduce((out, a, i, all) => {
    if (a.startsWith("--")) out.push([a.slice(2), all[i + 1]?.startsWith("--") ? true : all[i + 1] ?? true]);
    return out;
  }, []),
);
const server = String(args.server || "ws://127.0.0.1:8787").replace(/\/$/, "");
const room = String(args.room || "").toUpperCase();
const count = Number(args.count || 3);
const PROTOCOL = 5;
if (!room) {
  console.error("usage: node bots.mjs --room ABCDE [--count 3] [--server ws://127.0.0.1:8787]");
  process.exit(2);
}

const MSG = { STATE: 1, HELLO: 2, SETTINGS: 10, ROSTER: 11, ROUND: 12, PHASE: 13, FOUND: 14, RESULTS: 15, DECOYS: 16, READY: 20, HIT: 21, TAUNT: 23, PLACE_DECOY: 24, HIT_DECOY: 25 };
const ROLE = { NONE: 0, HIDER: 1, HUNTER: 2, SPECTATOR: 3 };
const PHASE = ["Lobby", "Gather", "Hide", "Seek", "Results"];
const FLAG = { IN_WORLD: 1, WOLF: 2, DISGUISED: 4, SWORD: 8, SHIELD: 16 };
const ANIM = { WAIT: 0x26a, RUN: 0xc5 };
const PROP_COUNT = 59;
const DISABLED_PROPS = new Set([36, 43, 47, 50, 57]);

function randomProp() {
  let prop;
  do prop = Math.floor(Math.random() * PROP_COUNT);
  while (DISABLED_PROPS.has(prop));
  return prop;
}

class Writer {
  constructor(type) {
    this.buf = new DataView(new ArrayBuffer(256));
    this.n = 0;
    this.u8(type);
  }
  u8(v) { this.buf.setUint8(this.n, v); this.n += 1; }
  s8(v) { this.buf.setInt8(this.n, v); this.n += 1; }
  u16(v) { this.buf.setUint16(this.n, v, true); this.n += 2; }
  s16(v) { this.buf.setInt16(this.n, v, true); this.n += 2; }
  u32(v) { this.buf.setUint32(this.n, v, true); this.n += 4; }
  f32(v) { this.buf.setFloat32(this.n, v, true); this.n += 4; }
  fixed(s, len) { for (let i = 0; i < len; ++i) this.u8(i < s.length ? s.charCodeAt(i) : 0); }
  bytes() { return new Uint8Array(this.buf.buffer, 0, this.n); }
}

class Reader {
  constructor(bytes, offset) {
    this.v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.n = offset;
  }
  u8() { return this.v.getUint8(this.n++); }
  s8() { return this.v.getInt8(this.n++); }
  u16() { const x = this.v.getUint16(this.n, true); this.n += 2; return x; }
  s16() { const x = this.v.getInt16(this.n, true); this.n += 2; return x; }
  u32() { const x = this.v.getUint32(this.n, true); this.n += 4; return x; }
  f32() { const x = this.v.getFloat32(this.n, true); this.n += 4; return x; }
  fixed(len) { let s = ""; for (let i = 0; i < len; ++i) { const c = this.u8(); if (c) s += String.fromCharCode(c); } return s; }
}

function readState(r) {
  const s = { flags: r.u8(), stage: r.fixed(8), room: r.s8(), x: r.f32(), y: r.f32(), z: r.f32(), yaw: r.s16(), prop: r.u8(), propYaw: r.s16() };
  return s;
}

function writeState(w, s) {
  w.u8(s.flags);
  w.fixed(s.stage, 8);
  w.s8(s.room);
  w.f32(s.x); w.f32(s.y); w.f32(s.z);
  w.s16(s.yaw);
  w.u8(s.prop);
  w.s16(s.propYaw);
  for (const a of [...s.under, ...s.upper]) { w.u16(a.idx); w.f32(a.frame); w.u8(a.ratio); }
}

const slot = (idx, frame) => ({ idx, frame, ratio: 0 });
const none = () => ({ idx: 0xffff, frame: 0, ratio: 0 });

class Bot {
  constructor(n) {
    this.n = n;
    this.name = `Bot ${n}`;
    this.id = 0;
    this.host = 0;
    this.roles = new Map();
    this.others = new Map(); // id -> latest state
    this.bots = new Set();
    this.phase = 0;
    this.round = 0;
    this.mode = 0;
    this.pos = null;
    this.stage = "";
    this.yaw = 0;
    this.prop = randomProp();
    this.spot = null;
    this.readyFor = 0;
    this.lastTaunt = Date.now();
    this.moving = false;
  }

  start() {
    const url = `${server}/join/${room}?name=${encodeURIComponent(this.name)}&v=${PROTOCOL}`;
    this.ws = new WebSocket(url);
    this.ws.binaryType = "arraybuffer";
    this.ws.addEventListener("message", (e) => this.onMessage(e.data));
    this.ws.addEventListener("close", () => {
      console.log(`${this.name}: disconnected`);
      clearInterval(this.timer);
    });
    this.timer = setInterval(() => this.tick(), 100);
  }

  send(to, bytes) {
    if (this.ws.readyState !== WebSocket.OPEN) return;
    const frame = new Uint8Array(bytes.length + 1);
    frame[0] = to;
    frame.set(bytes, 1);
    this.ws.send(frame);
  }

  onMessage(data) {
    if (typeof data === "string") {
      const msg = JSON.parse(data);
      if (msg.op === "welcome") {
        this.id = msg.you;
        this.host = msg.host;
        for (const p of msg.players) if (p.name.startsWith("Bot ")) this.bots.add(p.id);
        console.log(`${this.name}: joined ${msg.code} as ${this.id}`);
        const hello = new Writer(MSG.HELLO);
        hello.u8((this.n * 3) % 16);
        this.send(255, hello.bytes());
      } else if (msg.op === "join" && msg.name.startsWith("Bot ")) {
        this.bots.add(msg.id);
      } else if (msg.op === "host") {
        this.host = msg.id;
      } else if (msg.op === "error") {
        console.log(`${this.name}: refused: ${msg.why} ${msg.detail || ""}`);
      }
      return;
    }
    const bytes = new Uint8Array(data);
    const from = bytes[0];
    const r = new Reader(bytes, 2);
    switch (bytes[1]) {
      case MSG.STATE:
        this.others.set(from, readState(r));
        break;
      case MSG.ROSTER: {
        const n = r.u8();
        for (let i = 0; i < n; ++i) {
          const id = r.u8(); const role = r.u8(); r.u8(); r.u16(); r.u16(); r.u8(); r.u8(); r.u8();
          this.roles.set(id, role);
        }
        break;
      }
      case MSG.ROUND:
        this.round = r.u32();
        this.mode = r.u8();
        this.spot = null;
        this.prop = randomProp();
        console.log(`${this.name}: round ${this.round}, I'm a ${this.myRole() === ROLE.HUNTER ? "hunter" : "prop"}`);
        break;
      case MSG.PHASE:
        r.u32();
        this.phase = r.u8();
        break;
      case MSG.FOUND: {
        r.u32();
        const target = r.u8();
        const by = r.u8();
        if (target === this.id) console.log(`${this.name}: found by player ${by}!`);
        break;
      }
      case MSG.RESULTS:
        r.u32();
        console.log(`${this.name}: round over, ${r.u8() === 1 ? "hunters" : "props"} win`);
        break;
    }
  }

  myRole() {
    return this.roles.get(this.id) ?? ROLE.NONE;
  }

  // The first real player: bots gather around them.
  human() {
    for (const [id, s] of this.others) {
      if (!this.bots.has(id) && id !== this.id && s.flags & FLAG.IN_WORLD) return { id, s };
    }
    return null;
  }

  tick() {
    if (!this.id) return;
    const h = this.human();
    if (!h) return;
    const now = Date.now();
    const angle = (this.n / count) * Math.PI * 2;
    const hider = this.myRole() === ROLE.HIDER;
    const hunter = this.myRole() === ROLE.HUNTER;
    const playing = this.phase === 2 || this.phase === 3;

    // Follow the human between stages (the game warps them; we just copy the stage).
    if (h.s.stage !== this.stage || !this.pos) {
      this.stage = h.s.stage;
      this.pos = { x: h.s.x + Math.cos(angle) * 250, y: h.s.y, z: h.s.z + Math.sin(angle) * 250 };
      this.spot = null;
    }
    if (this.phase === 1 && this.readyFor !== this.round) {
      this.readyFor = this.round;
      const w = new Writer(MSG.READY);
      w.u32(this.round);
      this.send(255, w.bytes());
    }

    let target = { x: h.s.x + Math.cos(angle) * 250, y: h.s.y, z: h.s.z + Math.sin(angle) * 250 };
    if (playing && hider) {
      if (!this.spot) {
        const a = Math.random() * Math.PI * 2;
        const d = 300 + Math.random() * 500;
        this.spot = { x: h.s.x + Math.cos(a) * d, y: h.s.y, z: h.s.z + Math.sin(a) * d };
      }
      target = this.spot;
    } else if (playing && hunter) {
      target = this.phase === 3 ? { x: h.s.x, y: h.s.y, z: h.s.z } : this.pos;
    }
    const dx = target.x - this.pos.x;
    const dz = target.z - this.pos.z;
    const dist = Math.hypot(dx, dz);
    const step = Math.min(dist, 30); // 300 units a second
    this.moving = dist > 20 && !(hunter && this.phase === 2);
    if (this.moving) {
      this.pos.x += (dx / dist) * step;
      this.pos.z += (dz / dist) * step;
      this.yaw = Math.round((Math.atan2(dx, dz) / Math.PI) * 32768) | 0;
    }
    this.pos.y = h.s.y;

    if (hunter && this.phase === 3 && dist < 120 && this.roles.get(h.id) === ROLE.HIDER) {
      const w = new Writer(MSG.HIT);
      w.u32(this.round);
      w.u8(h.id);
      this.send(255, w.bytes());
    }
    if (hider && this.phase === 3 && now - this.lastTaunt > 12000 + this.n * 1500) {
      this.lastTaunt = now;
      const w = new Writer(MSG.TAUNT);
      w.u8(Math.floor(Math.random() * 6));
      this.send(0, w.bytes());
    }

    const t = now / 1000;
    const anim = this.moving ? slot(ANIM.RUN, (t * 30) % 20) : slot(ANIM.WAIT, (t * 30) % 60);
    anim.ratio = 255;
    const disguised = this.mode === 0 && hider && playing;
    const s = {
      flags: FLAG.IN_WORLD | (disguised ? FLAG.DISGUISED : 0) | (hunter && this.phase === 3 ? FLAG.SWORD | FLAG.SHIELD : 0),
      stage: this.stage,
      room: h.s.room,
      x: this.pos.x, y: this.pos.y, z: this.pos.z,
      yaw: (this.yaw << 16) >> 16,
      prop: this.prop,
      propYaw: 0,
      under: [anim, none(), none()],
      upper: [anim, none(), none()],
    };
    const w = new Writer(MSG.STATE);
    writeState(w, s);
    this.send(0, w.bytes());
  }
}

console.log(`joining ${room} on ${server} with ${count} bots`);
for (let i = 1; i <= count; ++i) setTimeout(() => new Bot(i).start(), i * 300);
