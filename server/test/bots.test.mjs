// Runs bots.mjs against the Node relay with a fake game host, checking the bots speak the mod's
// wire format (src/protocol.hpp): they follow the host's stage, report READY, and disguise as props.
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { startServer } from "../node-server.mjs";

let server;
let base;
// RELAY_URL=ws://127.0.0.1:8787 runs these against another relay, e.g. `wrangler dev`.
before(async () => {
  if (process.env.RELAY_URL) {
    base = process.env.RELAY_URL.replace(/\/$/, "");
    return;
  }
  server = await startServer({ port: 0, log: () => {}, limits: { maxPerIp: 100 } });
  base = `ws://127.0.0.1:${server.port}`;
});
after(async () => {
  if (server) await server.close();
});

function stateBytes({ stage, x, y, z, flags = 1 }) {
  const b = new DataView(new ArrayBuffer(71));
  let n = 0;
  b.setUint8(n++, 0); // relay "to everyone"
  b.setUint8(n++, 1); // MSG_STATE
  b.setUint8(n++, flags);
  for (let i = 0; i < 8; ++i) b.setUint8(n++, i < stage.length ? stage.charCodeAt(i) : 0);
  b.setInt8(n++, 0);
  b.setFloat32(n, x, true); n += 4;
  b.setFloat32(n, y, true); n += 4;
  b.setFloat32(n, z, true); n += 4;
  b.setInt16(n, 0, true); n += 2;
  b.setUint8(n++, 0);
  b.setInt16(n, 0, true); n += 2;
  for (let i = 0; i < 6; ++i) { b.setUint16(n, 0xffff, true); n += 2; b.setFloat32(n, 0, true); n += 4; b.setUint8(n++, 0); }
  assert.equal(n, 71);
  return new Uint8Array(b.buffer);
}

test("bots follow the host, get ready and turn into props", async () => {
  const host = new WebSocket(`${base}/host?name=Human&v=4`);
  host.binaryType = "arraybuffer";
  const states = new Map(); // bot id -> latest decoded state
  const readies = new Set();
  let code = "";
  await new Promise((resolve) => {
    host.addEventListener("message", (e) => {
      if (typeof e.data === "string") {
        const m = JSON.parse(e.data);
        if (m.op === "welcome") {
          code = m.code;
          resolve();
        }
        return;
      }
      const b = new Uint8Array(e.data);
      const v = new DataView(b.buffer);
      if (b[1] === 1) {
        const stage = String.fromCharCode(...b.slice(3, 11)).replace(/\0+$/, "");
        states.set(b[0], { flags: b[2], stage, x: v.getFloat32(12, true), z: v.getFloat32(20, true), prop: b[26] });
      } else if (b[1] === 20) {
        readies.add(b[0]);
      }
    });
  });

  const bots = spawn(process.execPath, [fileURLToPath(new URL("../bots.mjs", import.meta.url)), "--room", code, "--count", "2", "--server", base], { stdio: "ignore" });
  try {
    const send = (bytes) => host.send(bytes);
    const tick = setInterval(() => send(stateBytes({ stage: "F_SP109", x: 1000, y: 0, z: 2000 })), 100);
    await new Promise((r) => setTimeout(r, 1500));
    assert.equal(states.size, 2);
    for (const s of states.values()) {
      assert.equal(s.stage, "F_SP109");
      assert.ok(Math.hypot(s.x - 1000, s.z - 2000) < 600, "bots stand near the host");
    }

    // Round 1: both bots are props (roster: host hunter, bots 2 and 3 hiders).
    const roster = new Uint8Array([
      0, 11, 3,
      1, 2, 0, 0, 0, 0, 0, 0, 1, 0,
      2, 1, 1, 0, 0, 0, 0, 0, 0, 0,
      3, 1, 2, 0, 0, 0, 0, 0, 0, 0,
    ]);
    send(roster);
    send(new Uint8Array([0, 12, 1, 0, 0, 0, 0, 4, 45, 0, 240, 0])); // ROUND 1, prop hunt, map 4
    send(new Uint8Array([0, 13, 1, 0, 0, 0, 1, 0x10, 0x27, 0, 0])); // PHASE gather
    await new Promise((r) => setTimeout(r, 400));
    assert.deepEqual([...readies].sort(), [2, 3]);
    send(new Uint8Array([0, 13, 1, 0, 0, 0, 2, 0x10, 0x27, 0, 0])); // PHASE hide
    await new Promise((r) => setTimeout(r, 400));
    for (const s of states.values()) {
      assert.ok(s.flags & 4, "disguised during the hide phase");
      assert.ok(s.prop < 59);
    }
    clearInterval(tick);
  } finally {
    bots.kill();
    host.close();
  }
});
