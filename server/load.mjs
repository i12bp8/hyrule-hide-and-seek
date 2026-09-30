// Bounded local relay benchmark. No Cloudflare requests or free-tier quota are consumed.
import { WebSocket } from "ws";
import { startServer } from "./node-server.mjs";
const count = Number(process.argv[2] || 200);
const seconds = Number(process.argv[3] || 10);
if (!Number.isInteger(count) || count < 2 || count > 400 || seconds < 1 || seconds > 60) throw new Error("usage: node load.mjs [2..400 players] [1..60 seconds]");
const relay = await startServer({ port: 0, log: () => {}, limits: { maxPerIp: 500, roomsPerIpPerMinute: 100 } });
const sockets = [], latency = [];
let received = 0, sent = 0, crossedRooms = 0;
const rooms = [];
async function connect(path, roomIndex) {
  const ws = new WebSocket(`ws://127.0.0.1:${relay.port}${path}`);
  sockets.push(ws);
  return new Promise((resolve, reject) => {
    ws.once("error", reject);
    ws.on("message", (data, binary) => {
      if (!binary) {
        const m = JSON.parse(data);
        if (m.op === "welcome") resolve({ ws, code: m.code });
        if (m.op === "error") reject(new Error(m.why));
        return;
      }
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      if (view.getUint16(2, true) !== roomIndex) crossedRooms++;
      latency.push(performance.now() - view.getFloat64(8, true)); received++;
    });
  });
}
try {
  for (let n = 0; n < count;) {
    const index = rooms.length;
    const host = await connect(`/host?name=Load${n++}&v=7`, index);
    const room = [host.ws]; rooms.push(room);
    while (room.length < 8 && n < count) {
      const peer = await connect(`/join/${host.code}?name=Load${n++}&v=7`, index); room.push(peer.ws);
    }
  }
  const iterations = seconds * 10;
  for (let i = 0; i < iterations; i++) {
    for (let r = 0; r < rooms.length; r++) for (const ws of rooms[r]) {
      const bytes = new Uint8Array(16), view = new DataView(bytes.buffer);
      bytes[0] = 0; bytes[1] = 1; view.setUint16(2, r, true); view.setUint32(4, i, true); view.setFloat64(8, performance.now(), true);
      ws.send(bytes); sent++;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await new Promise((resolve) => setTimeout(resolve, 300));
  const expected = rooms.reduce((n, room) => n + room.length * (room.length - 1) * iterations, 0);
  latency.sort((a, b) => a - b);
  console.log(JSON.stringify({ players: count, rooms: rooms.length, seconds, sent, expected, received, crossedRooms,
    medianMs: Number(latency[Math.floor(latency.length * 0.5)]?.toFixed(2)),
    p95Ms: Number(latency[Math.floor(latency.length * 0.95)]?.toFixed(2)),
    maxMs: Number(latency.at(-1)?.toFixed(2)) }, null, 2));
  if (received !== expected || crossedRooms) process.exitCode = 1;
} finally {
  for (const ws of sockets) ws.terminate();
  await relay.close();
}
