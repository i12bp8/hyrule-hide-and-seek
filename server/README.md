# Relay server

Every player connects here with one outgoing `wss://` connection. The relay only keeps rooms and
forwards bytes between the players in a room; the host's game runs the rules. Because nobody
connects to anybody directly, it works on every network with no port forwarding.

The same room logic (`src/room.js`) runs on Cloudflare (`src/index.js`) and on plain Node
(`node-server.mjs`).

## Put it online on Cloudflare (free, about five minutes)

You need [Node.js](https://nodejs.org) and a free [Cloudflare account](https://dash.cloudflare.com/sign-up).
No credit card.

```sh
cd server
npm install
npx wrangler login
npx wrangler deploy
```

`wrangler deploy` prints an address like `https://hyrule-hide-and-seek.<you>.workers.dev`. Change
`https` to `wss` and put it in `HS_DEFAULT_SERVER` in the top-level `CMakeLists.txt`, then build a
release. Every copy of the mod then uses it.

### What it costs

Cloudflare bills a Durable Object's incoming WebSocket messages at 1/20th of a request. Players send
10 updates a second, so an 8-player game is about 15,000 requests an hour.

- Free plan: 100,000 requests a day, roughly 7 game-hours a day across all players. Past that, new
  games fail until midnight UTC. Nothing is charged.
- Workers Paid ($5 a month): includes about 65 game-hours of requests a month. After that, requests
  ($0.15 per million) plus Durable Object time ($12.50 per million GB-s, ~460 GB-s per room-hour)
  come to roughly $0.80 per 100 game-hours. Check Cloudflare's current prices before relying on this.

A room only costs anything while players are in it.

## Run it anywhere else

```sh
npm install
node node-server.mjs --host 0.0.0.0 --port 8787
```

The game only allows plain `ws://` to your own computer, so for other people put it behind a TLS
proxy, e.g. Caddy: `your.domain { reverse_proxy 127.0.0.1:8787 }`, and use `wss://your.domain`.

## Test

```sh
npm test                                   # against the Node relay
npx wrangler dev --port 8788 &             # the Cloudflare version, locally
RELAY_URL=ws://127.0.0.1:8788 npm test     # the same tests against it
```

## Test bots

```sh
node node-server.mjs
node bots.mjs --room ABCDE --count 3 [--server ws://127.0.0.1:8787]
```

Host a room in the game first (Settings → Use a server on this computer), then start the bots with
its code. They stand near you, follow you to the round's map, disguise themselves as props when
they hide (hit them!), and chase and tag you when they hunt.

## Protocol

See the top of [src/room.js](src/room.js) for the relay framing and control messages, and
[../src/protocol.hpp](../src/protocol.hpp) for the game messages. Bump `kProtocolVersion` in
`protocol.hpp` (and `PROTOCOL` in `bots.mjs`) on any change; the relay keeps different versions
apart.
