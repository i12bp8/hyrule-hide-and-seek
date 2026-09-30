import { test } from "node:test";
import assert from "node:assert/strict";
import { DatabaseSync } from "node:sqlite";
import { Lobby } from "../src/index.js";

test("Cloudflare public listings survive reconstruction and filter expired/full/versioned rooms", async () => {
  const db = new DatabaseSync(":memory:");
  const ctx = { storage: { sql: { exec(query, ...values) {
    const statement = db.prepare(query);
    if (query.startsWith("SELECT")) return statement.all(...values);
    statement.run(...values);
    return [];
  } } } };
  const old = new Lobby(ctx);
  const entry = { code: "ABCDE", label: "Hunt", players: 3, max: 16, mode: 0, map: 1, phase: 3, v: 7, at: Date.now() };
  const publish = (e) => old.fetch(new Request("https://lobby/publish", { method: "POST", body: JSON.stringify(e) }));
  try {
    await publish(entry);
    await publish({ ...entry, code: "FULLL", players: 16 });
    await publish({ ...entry, code: "OLDVR", v: 6 });
    await publish({ ...entry, code: "STALE", at: Date.now() - 200000 });
    const restored = new Lobby(ctx); // same storage, no previous in-memory state
    const response = await restored.fetch(new Request("https://lobby/list?v=7"));
    const { rooms } = await response.json();
    assert.deepEqual(rooms.map((r) => r.code), ["ABCDE"]);
    assert.equal(rooms[0].players, 3);
    await restored.fetch(new Request("https://lobby/unpublish", { method: "POST", body: JSON.stringify({ code: "ABCDE" }) }));
    assert.equal((await (await new Lobby(ctx).fetch(new Request("https://lobby/list?v=7"))).json()).rooms.length, 0);
  } finally { db.close(); }
});
