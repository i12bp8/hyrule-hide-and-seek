import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { startServer } from "../node-server.mjs";
import { decodeHttpBatch, encodeHttpBatch } from "../src/room.js";

let server;
let base;

// RELAY_URL=ws://127.0.0.1:8787 runs these against another relay, e.g. `wrangler dev`.
before(async () => {
  if (process.env.RELAY_URL) {
    base = process.env.RELAY_URL.replace(/\/$/, "");
    return;
  }
  server = await startServer({ port: 0, log: () => {}, limits: { maxPerIp: 100, roomsPerIpPerMinute: 100 } });
  base = `ws://127.0.0.1:${server.port}`;
});

after(async () => {
  if (server) await server.close();
});

// A test client that queues everything it receives.
function connect(path) {
  const ws = new WebSocket(`${base}${path}`);
  ws.binaryType = "arraybuffer";
  const inbox = [];
  const waiters = [];
  ws.addEventListener("message", (e) => {
    const msg = typeof e.data === "string" ? JSON.parse(e.data) : new Uint8Array(e.data);
    const w = waiters.findIndex((x) => x.match(msg));
    if (w >= 0) waiters.splice(w, 1)[0].resolve(msg);
    else inbox.push(msg);
  });
  const closed = new Promise((resolve) => ws.addEventListener("close", (e) => resolve(e.code)));
  return {
    ws,
    closed,
    next(match = () => true, ms = 2000) {
      const i = inbox.findIndex(match);
      if (i >= 0) return Promise.resolve(inbox.splice(i, 1)[0]);
      return new Promise((resolve, reject) => {
        const entry = { match, resolve };
        waiters.push(entry);
        setTimeout(() => {
          const k = waiters.indexOf(entry);
          if (k >= 0) {
            waiters.splice(k, 1);
            reject(new Error("timed out waiting for a message"));
          }
        }, ms);
      });
    },
    op(name) {
      return this.next((m) => !(m instanceof Uint8Array) && m.op === name);
    },
    binary() {
      return this.next((m) => m instanceof Uint8Array);
    },
    send(to, ...bytes) {
      ws.send(new Uint8Array([to, ...bytes]));
    },
    json(obj) {
      ws.send(JSON.stringify(obj));
    },
    close() {
      ws.close();
      return closed;
    },
  };
}

async function host(name = "Host", v = 1) {
  const c = connect(`/host?name=${encodeURIComponent(name)}&v=${v}`);
  const welcome = await c.op("welcome");
  return { c, welcome };
}

async function join(code, name, v = 1) {
  const c = connect(`/join/${code}?name=${encodeURIComponent(name)}&v=${v}`);
  const welcome = await c.op("welcome");
  return { c, welcome };
}

function httpUrl(path) {
  return `${base.replace(/^ws/, "http")}${path}`;
}

async function httpOpen(path) {
  const response = await fetch(httpUrl(`/session${path}`), { method: "POST" });
  assert.equal(response.status, 200);
  return (await response.json()).session;
}

async function httpPoll(session) {
  const response = await fetch(httpUrl(`/session/${session}/poll`));
  assert.equal(response.status, 200);
  return decodeHttpBatch(await response.arrayBuffer());
}

async function httpSend(session, messages) {
  const response = await fetch(httpUrl(`/session/${session}/send`), {
    method: "POST",
    headers: { "content-type": "application/octet-stream" },
    body: encodeHttpBatch(messages),
  });
  assert.equal(response.status, 204);
}

async function httpLeave(session) {
  const response = await fetch(httpUrl(`/session/${session}/leave`), { method: "POST" });
  assert.equal(response.status, 204);
}

test("host gets a five letter code and id 1", async () => {
  const { c, welcome } = await host();
  assert.match(welcome.code, /^[A-Z]{5}$/);
  assert.equal(welcome.you, 1);
  assert.equal(welcome.host, 1);
  assert.deepEqual(welcome.players, [{ id: 1, name: "Host" }]);
  await c.close();
});

test("joining is announced and bytes are forwarded with the sender id", async () => {
  const h = await host("Midna");
  const j = await join(h.welcome.code.toLowerCase(), "Ilia");
  assert.equal(j.welcome.you, 2);
  assert.equal(j.welcome.host, 1);
  assert.deepEqual(await h.c.op("join"), { op: "join", id: 2, name: "Ilia" });

  j.c.send(0, 7, 8, 9);
  assert.deepEqual([...(await h.c.binary())], [2, 7, 8, 9]);

  h.c.send(2, 42);
  assert.deepEqual([...(await j.c.binary())], [1, 42]);

  await j.c.close();
  await h.c.close();
});

test("HTTP fallback hosts, joins, forwards messages and leaves", async () => {
  const hostSession = await httpOpen("/host?name=Midna&v=9");
  const hostWelcome = JSON.parse((await httpPoll(hostSession))[0]);
  assert.equal(hostWelcome.op, "welcome");
  assert.equal(hostWelcome.you, 1);

  const joinSession = await httpOpen(`/join/${hostWelcome.code}?name=Ilia&v=9`);
  const joinWelcome = JSON.parse((await httpPoll(joinSession))[0]);
  assert.equal(joinWelcome.you, 2);
  assert.deepEqual(JSON.parse((await httpPoll(hostSession))[0]), { op: "join", id: 2, name: "Ilia" });

  await httpSend(joinSession, [new Uint8Array([0, 7, 8, 9])]);
  assert.deepEqual([...(await httpPoll(hostSession))[0]], [2, 7, 8, 9]);

  await httpSend(hostSession, [
    JSON.stringify({ op: "meta", public: true, label: "HTTP room", mode: 0, map: 3, phase: 2 }),
  ]);
  await new Promise((resolve) => setTimeout(resolve, 50));
  const listed = await (await fetch(httpUrl("/rooms?v=9"))).json();
  assert.deepEqual(listed.rooms.map((room) => [room.code, room.label, room.players]), [
    [hostWelcome.code, "HTTP room", 2],
  ]);

  await httpLeave(joinSession);
  assert.deepEqual(JSON.parse((await httpPoll(hostSession))[0]), { op: "leave", id: 2 });
  await httpLeave(hostSession);
});

test("HTTP fallback and WebSocket players can share a room", async () => {
  const hostPlayer = await host("Zelda", 4);
  const session = await httpOpen(`/join/${hostPlayer.welcome.code}?name=Link&v=4`);
  const welcome = JSON.parse((await httpPoll(session))[0]);
  assert.equal(welcome.you, 2);
  assert.deepEqual(await hostPlayer.c.op("join"), { op: "join", id: 2, name: "Link" });

  hostPlayer.c.send(0, 11, 12);
  assert.deepEqual([...(await httpPoll(session))[0]], [1, 11, 12]);
  await httpSend(session, [new Uint8Array([0, 21, 22])]);
  assert.deepEqual([...(await hostPlayer.c.binary())], [2, 21, 22]);

  await httpLeave(session);
  assert.deepEqual(await hostPlayer.c.op("leave"), { op: "leave", id: 2 });
  await hostPlayer.c.close();
});

test("to-host messages reach only the host", async () => {
  const h = await host();
  const a = await join(h.welcome.code, "A");
  const b = await join(h.welcome.code, "B");
  b.c.send(255, 1);
  assert.deepEqual([...(await h.c.binary())], [3, 1]);
  await assert.rejects(a.c.binary().then(() => {}), /timed out/);
  for (const x of [a, b, h]) await x.c.close();
});

test("duplicate names get a number", async () => {
  const h = await host("Link");
  const j = await join(h.welcome.code, "link");
  assert.equal(j.welcome.players.find((p) => p.id === 2).name, "link 2");
  await j.c.close();
  await h.c.close();
});

test("the lowest remaining id becomes host when the host leaves", async () => {
  const h = await host();
  const a = await join(h.welcome.code, "A");
  const b = await join(h.welcome.code, "B");
  await h.c.close();
  assert.deepEqual(await a.c.op("leave"), { op: "leave", id: 1 });
  assert.deepEqual(await a.c.op("host"), { op: "host", id: 2 });
  assert.deepEqual(await b.c.op("host"), { op: "host", id: 2 });
  b.c.send(255, 5);
  assert.deepEqual([...(await a.c.binary())], [3, 5]);
  await a.c.close();
  await b.c.close();
});

test("a different game version is refused", async () => {
  const h = await host("Host", 3);
  const c = connect(`/join/${h.welcome.code}?name=Old&v=2`);
  const err = await c.op("error");
  assert.equal(err.why, "version");
  assert.equal(err.detail, "3");
  assert.equal(await c.closed, 4000);
  await h.c.close();
});

test("an unknown room is refused", async () => {
  const c = connect(`/join/QQQQQ?name=X&v=1`);
  assert.equal((await c.op("error")).why, "no_room");
  await c.closed;
});

test("rooms hold sixteen players", async () => {
  const h = await host();
  const players = [];
  for (let i = 2; i <= 16; ++i) players.push(await join(h.welcome.code, `P${i}`));
  const extra = connect(`/join/${h.welcome.code}?name=Late&v=1`);
  assert.equal((await extra.op("error")).why, "full");
  for (const p of players) await p.c.close();
  await h.c.close();
});

test("public rooms are listed and disappear when they go private", async () => {
  const h = await host("Ashei", 7);
  h.c.json({ op: "meta", public: true, label: "Snowpeak fans", mode: 1, map: 4, phase: 0 });
  await new Promise((r) => setTimeout(r, 50));
  const http = base.replace("ws://", "http://");
  let list = await (await fetch(`${http}/rooms?v=7`)).json();
  assert.deepEqual(list.rooms.map((r) => [r.code, r.label, r.players, r.mode, r.map]), [
    [h.welcome.code, "Snowpeak fans", 1, 1, 4],
  ]);
  list = await (await fetch(`${http}/rooms?v=6`)).json();
  assert.equal(list.rooms.length, 0);

  h.c.json({ op: "meta", public: false });
  await new Promise((r) => setTimeout(r, 50));
  list = await (await fetch(`${http}/rooms?v=7`)).json();
  assert.equal(list.rooms.length, 0);
  await h.c.close();
});

test("only the host can kick", async () => {
  const h = await host();
  const a = await join(h.welcome.code, "A");
  const b = await join(h.welcome.code, "B");
  a.c.json({ op: "kick", id: 3 });
  h.c.json({ op: "kick", id: 2 });
  assert.equal((await a.c.op("error")).why, "kicked");
  assert.deepEqual(await b.c.op("leave"), { op: "leave", id: 2 });
  await b.c.close();
  await h.c.close();
});

test("one address can't flood the server", { skip: !!process.env.RELAY_URL }, async () => {
  const small = await startServer({ port: 0, log: () => {}, limits: { maxPerIp: 3, roomsPerIpPerMinute: 2 } });
  const url = `ws://127.0.0.1:${small.port}`;
  const opened = [];
  const attempt = (path) =>
    new Promise((resolve) => {
      const ws = new WebSocket(`${url}${path}`);
      ws.addEventListener("message", (e) => {
        const m = JSON.parse(e.data);
        if (m.op === "welcome") {
          opened.push(ws);
          resolve(m);
        }
      });
      ws.addEventListener("error", () => resolve(null));
    });
  const a = await attempt("/host?name=A&v=1");
  const b = await attempt("/host?name=B&v=1");
  assert.ok(a && b);
  assert.equal(await attempt("/host?name=C&v=1"), null, "third room in a minute is refused");
  assert.ok(await attempt(`/join/${a.code}?name=D&v=1`));
  assert.equal(await attempt(`/join/${a.code}?name=E&v=1`), null, "fourth connection is refused");
  for (const ws of opened) ws.close();
  await small.close();
});

test("a proxy's forwarded address is used only when trusted", { skip: !!process.env.RELAY_URL }, async () => {
  const proxied = await startServer({ port: 0, log: () => {}, trustProxy: true, limits: { maxPerIp: 1 } });
  const url = `ws://127.0.0.1:${proxied.port}`;
  const { WebSocket: NodeWs } = await import("ws");
  const connect = (xff) =>
    new Promise((resolve) => {
      const ws = new NodeWs(`${url}/host?name=X&v=1`, { headers: { "x-forwarded-for": xff } });
      ws.on("message", () => resolve(ws));
      ws.on("error", () => resolve(null));
      ws.on("unexpected-response", () => resolve(null));
    });
  // A client can prepend anything; the proxy's own entry (the last one) counts.
  const one = await connect("6.6.6.6, 10.0.0.1");
  const two = await connect("7.7.7.7, 10.0.0.2");
  const three = await connect("8.8.8.8, 10.0.0.1");
  assert.ok(one && two);
  assert.equal(three, null);
  one.close();
  two.close();
  await proxied.close();
});
