# Relay server

Default: `wss://hyrule-hide-and-seek.jhackerr.workers.dev`.
Each room is an independent SQLite-backed Cloudflare Durable Object with at most 16 players.
The host's game runs the rules; the relay forwards messages within that room. Clients never expose
their home IP addresses to other players or accept incoming connections.

Use Dusklight **2.0.3 or newer**. The official Linux WebSocket backend and HTTPS CA fix are in
[v2.0.3](https://github.com/TwilitRealm/dusklight/releases/tag/v2.0.3). The public Worker disables
legacy HTTP polling and responds with an actionable upgrade message. A private Node relay retains
the fallback for older clients. Everyone in a room must use the same game protocol (currently 11).

## Deploy with Wrangler

```sh
cd server
npm ci
npx wrangler login
npx wrangler deploy
```

Wrangler prints the `https://` endpoint. Use `wss://` in the mod's Settings or the
`HS_DEFAULT_SERVER` CMake option. This configuration uses SQLite Durable Objects available on
Workers Free; it does not upgrade an account to a paid subscription. Existing account subscriptions
still determine billing. Check the account's Workers plan before inviting a large audience.

## Free tier capacity

As checked on 2026-09-30, Cloudflare's Durable Objects Free allowance is **100,000 metered requests
and 13,000 GB-s per day**, plus SQLite storage allowances. Incoming WebSocket messages count at
**20:1**; outgoing messages do not count. Connections, lobby requests and updates also use quota.
Allowances are shared with other Workers on the account. Operations exceeding a free allowance
fail until its daily reset at 00:00 UTC. See
[Cloudflare's current pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/).

For position updates alone:

| Player activity | Send rate | Metered requests per player-hour | Ideal daily player-hours |
| --- | ---: | ---: | ---: |
| Moving during a round | 10 Hz | 1,800 | 55.6 |
| Stationary during a round | 2 Hz | 360 | 277.8 |
| Stationary lobby | 1 Hz | 180 | 555.6 |

These are request-only upper bounds, excluding all other messages, joins, browsing, storage and
duration. For example, 200 continuously moving players use about 100,000 requests in 17 minutes.
Smaller payloads save bandwidth; they do not reduce the per-message count. Hibernation saves idle
duration, but active play still consumes duration. Measure actual usage in the Cloudflare dashboard
before treating any number as capacity. There is no honest unlimited-free guarantee for hundreds
of active players all day.

### What keeps usage down

- One Durable Object per room and bounded fan-out to 15 other players.
- `acceptWebSocket` with attachments for hibernation; no room timers or continuous server tick.
- Latest-only state queue during client backpressure; rules/results have a separate bounded queue.
- 10 Hz moving state, 2 Hz stationary state, 1 Hz stationary lobby state; disguises omit Link animation slots.
- Public listings use indexed SQLite and survive Lobby reconstruction after hibernation.
- Legacy polling is off by default (`ALLOW_HTTP_FALLBACK = "false"`).

## Run a private relay

```sh
npm ci
node node-server.mjs --host 0.0.0.0 --port 8787
```

Use `ws://127.0.0.1:8787` on your own computer. Put remote connections behind a TLS proxy and use
`wss://your.domain`. A private Node relay has no Cloudflare request quota; hardware, connectivity
and hosting costs still apply. See [Pi/Tailscale setup](deploy/README.md).

## Checks and bots

```sh
npm test                                      # Node relay + protocol + SQLite listing tests
npx wrangler dev --port 8788 --var ALLOW_HTTP_FALLBACK:true
# In another terminal; exercises both transports on the actual Worker runtime:
RELAY_URL=ws://127.0.0.1:8788 npm test
node load.mjs 200 10        # local Node load measurement
node bots.mjs --room ABCDE --count 3 --server ws://127.0.0.1:8787
```

Host a room in the game before starting bots. They follow the round's map, disguise as props when
hiding and chase hiders when hunting. The load tool creates independent eight-player rooms on a
local relay; its measurements are not worldwide latency or a Cloudflare free-capacity guarantee.

## Protocol

Relay framing and controls: [src/room.js](src/room.js). Game framing:
[../src/protocol.hpp](../src/protocol.hpp). Bump `kProtocolVersion` and the bots' `PROTOCOL` whenever
wire layouts change; different versions cannot join the same room.
