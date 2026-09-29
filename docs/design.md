# Hyrule Hide & Seek: design

Online hide and seek for Twilight Princess on [Dusklight](https://github.com/TwilitRealm/dusklight).
Install one `.dusk`, press **Host**, share the room code, play. No port forwarding, no setup.

## Goals

1. **Zero setup.** Hosting and joining are one button each. Works on home wifi, mobile data and
   school networks alike.
2. **Simple rules, fun from the first round.** Two modes, sensible defaults, host can tweak.
3. **Small and robust.** Every game hook is optional where possible; if something is missing the mod
   degrades instead of crashing.

## What already exists (research, 2026-09-28)

| Project | What it does | What we learn |
| --- | --- | --- |
| [Crests of Courage](https://github.com/remiafterdark/crests-of-courage) (MIT) | Online co-op, 2-16 players. Room server on Cloudflare only introduces players, then UDP hole punching; falls back to "install Tailscale". | Works, but strict networks (mobile, school) can't join. Its puppet renderer is 6k lines hooked deep into `daAlink_c`. |
| [MFB Multiplayer](https://github.com/thevipguy/dusklight-online-mod) (CC0) | Online co-op with a separate `daRemoteLink_c` actor that owns its own copy of Link's models. | Cleanest way to draw another Link: its own actor, own models, animations read from the global `AlAnm` archive by index. |
| SMO Online hide & seek (Super Mario Odyssey mod) | The reference hide & seek: hiders get a head start, seekers tag by touch, tagged hiders become seekers, the longest hider wins. 5 s tag immunity after loading a new area. | Proven rules. We copy them. |
| Garry's Mod Prop Hunt | Hiders become props, seekers attack suspicious props, hiders taunt. | Prop mode. Misses need a cost or seekers just swing at everything. |

### Why a relay instead of peer-to-peer

Peer-to-peer (Crests of Courage) is free to run but fails on networks with strict NAT, and then the
player has to install Tailscale. That breaks goal 1. This mod sends everything through a small relay:

- Every player makes one outgoing `wss://` connection (Dusklight's WebSocketService). If that
  service is unavailable, the client uses HTTPS long polling with the same messages. Both work
  everywhere HTTPS works.
- The relay is dumb: it only knows rooms and forwards bytes. The **host's game** runs the rules.
- It runs on Cloudflare Workers + Durable Objects (free tier, no credit card), or on any machine with
  Node.js (`server/node-server.mjs`).
- Cost estimate: an 8-player round sends about 80 small messages per second into the room, which
  Cloudflare bills at 1/20th of a request each: ~15k requests per game-hour. The free plan's 100k
  requests a day covers about 7 game-hours a day; past that the paid plan costs roughly $0.80 per
  100 game-hours (see server/README.md). The HTTP fallback uses more requests and reaches the free
  limit sooner.

"Join by address" (LAN / Tailscale, no server at all) can be added later on NetService with the same
protocol; it isn't needed for v1.

## Game design

Prop Hunt is the main mode; classic Hide & Seek comes along for free.

### Flow

1. **Lobby.** Everyone who joined the room walks around in their own game and sees the others, each
   as Link in their own tunic colour. The host picks mode, map (or Random) and times.
2. **Get ready.** Host presses Start. Everyone is warped to the map's spawn point and held there until
   everyone has loaded (max 25 s).
3. **Hide** (default 45 s). Props scatter and pick a disguise. Hunters stand at spawn with a black
   screen and a countdown.
4. **Hunt** (default 4 min). Hunters have to hit every prop with their sword before the timer runs
   out. A found prop becomes a hunter (default) or watches.
5. **Results** (12 s). Scoreboard, then the next round starts by itself with new hunters (the
   players who have hunted least go first).

### Prop Hunt

- Props: 59 objects drawn from every region, with map-themed selection, plus 12 deterministic
  scenery props in six hand-authored pairs beside native scenery. This gives sparse maps believable
  cover without a random pile at the hunter spawn. Models are read from the player's own game files.
- D-pad right copies the pot/crate/barrel you stand next to, like "become the prop you look at" in
  Garry's Mod; with nothing nearby it cycles. D-pad left goes back.
- Hunters hit props with a real sword swing: physical props carry a solid cylinder that Link cannot
  walk through and that only Link's sword (and wolf attacks) can hit. Grass, leaves, flowers and
  other soft scenery stay walk-through but remain sword targets. While swimming, where the game
  prevents sword use, B performs a close-range prop tag. A sword swing that hits no prop costs a
  quarter heart (never the last one).
- D-pad down taunts for a point. Hunters hear a Link shout and get a direction, distance and
  through-scenery world marker for five seconds. By default a prop that has not moved for 60 seconds
  automatically taunts; hosts can choose Off/30/45/60/90/120 seconds. Extra automatic taunts every
  20 s in the last minute remain optional and off by default.
- Hunters don't see props' name tags.

### Hide & Seek

Everyone stays Link; hunters tag hiders by touching them (SMO Online rules). A hider's name tag
shows up for hunters only up close.

### Scoring

- Props: 1 point per 10 s hidden during the hunt, +5 for surviving, +1 per taunt (every 5 s at most).
- Hunters: +5 per find.
- Totals are kept for the room until the host resets them.

### Maps (spawn points verified against the disc files)

Ordon Village, Ordon Ranch, Ordon Spring, South Faron Woods, Kakariko Village, Kakariko Graveyard,
Death Mountain Trail, Zora's Domain, Upper Zora's River, Lake Hylia, Castle Town, Sacred Grove,
Hidden Village, Gerudo Desert, Hyrule Field, or Random every round. A missing spawn point crashes the
game (`dStage_playerInit` logs a fatal error), so each map uses a point that `tools/find_spawns.py`
lists as standing with no event. A player is only visible to others in the same stage, so the round
keeps everyone inside it: scripted doors/events are inert, and a loading zone that leaves the
selected stage returns that player to the round spawn.

**World state.** Areas look different depending on story progress (twilight, NPCs). The
**Hide & Seek game mode** on the title screen uses its own completed-story save profile. Persistent
story, dungeon, side-quest, tutorial and forced-conversation flags select the calm late-game layers;
twilight and transformations are disabled; valid Postman letters are already delivered; first-time
rupee messages are cleared; and the clock stays at noon. Players get the Hero's Clothes, sword,
shield and six hearts, so every player's world matches and every hunter has a sword. Scripted event
orders are rejected, active events are reset, and enemy/boss actors plus encounter controllers are
removed before they can execute. Damage and drowning cannot end play (the optional hunter miss
penalty still can remove hearts). The live sandbox protections also apply during an online round
started from a normal save, but only the dedicated game mode applies the matching completed-story
save profile.

## Architecture

```
┌─────────── game (C++ mod) ───────────┐            ┌──── relay (Worker / Node) ────┐
│ ui.cpp      window: host/join/lobby   │            │ rooms: code → players (≤16)   │
│ game.cpp    rules, rounds, roles      │  wss://    │ forwards [to][payload]        │
│ net.cpp     WebSocket, reconnect      │◀──────────▶│ as [from][payload]            │
│ local.cpp   read Link, warp, freeze   │            │ JSON control: welcome, join,  │
│ puppet.cpp  draw other players/props  │            │ leave, host changes           │
│ hud.cpp     timer, banners, name tags │            │ /rooms: public room list      │
└───────────────────────────────────────┘            └───────────────────────────────┘
```

### Authority

The room's host (first player, or the next lowest id if the host leaves) is authoritative for the
round: roles, phase timers, tags and scores. Other clients send claims ("I tagged 4") and the host
checks them against its own view of both positions before announcing them. Every message the host
sends is also applied locally, so the host's own game follows the same code path.

### Wire protocol

Relay framing (binary WebSocket frames):

- client → relay: `u8 to` (0 = everyone else, 255 = host, 1-16 = one player) + payload
- relay → client: `u8 from` + payload

Control messages are JSON text frames (see `server/src/room.js`).

Game payloads start with a `u8` type; all numbers little-endian (`src/protocol.hpp`):

| Type | Dir | Content |
| --- | --- | --- |
| `STATE` | all, 10 Hz | stage, room, position, yaw, prop, sword/shield out, under/upper animation slots (69 bytes) |
| `SETTINGS` | host → all | mode, map, times, options |
| `ROSTER` | host → all | per player: role, score, found flag |
| `ROUND` | host → all | round id, map, seekers |
| `PHASE` | host → all | phase + milliseconds left |
| `HELLO` | → host | wanted tunic colour |
| `READY` | → host | "I've loaded into round N" |
| `HIT` | → host | "my sword hit / I touched player N" |
| `FOUND` | host → all | target, hunter |
| `RESULTS` | host → all | winner |
| `TAUNT` | all | sound id |

### Drawing other players

A custom actor (`HSPupt`, registered through ActorService) per remote player in the same stage:

- Link: the Hero's Clothes body, cap, face and hands, copied once out of `Kmdl.arc` (plus sword,
  sheath and Hylian Shield) into the mod's own heap. The body and cap are loaded once per tunic
  colour with the green shifted in the CMPR texture data (`src/recolor.cpp`), so every player looks
  different and none of it is shared with the local Link.
- Animation: a known-complete idle or walk pose from the global `AlAnm` archive, cached before map
  warps and retried after transient remount failures. A single full-skeleton blend calculator avoids
  cutscene-only or half-loaded sender animations making a remote Link disappear or T-pose.
- Props: the prop's archive model through the resource manager, drawn at the player's position;
  joint callbacks and animations the real objects put on the shared model data are swapped out
  around our `calc()`.
- Position is interpolated between 10 Hz updates. Carryables use their native simple-shadow sizes,
  actors such as Cuccos and targets use their native model-projection profile, and static or
  flat/translucent scenery casts no extra dynamic blob—matching the corresponding real actor.
- The local player's own tunic is recoloured with TextureService pointer-keyed replacements.

The local player in prop mode: Link's draw is skipped by a pre-hook on `daAlink_c::draw` and the
same puppet actor draws the prop at Link's position.

### HUD

A `dDlst_base_c` queued on the 2D translucent list each frame (same technique as Crests of Courage):
role and timer at the top, hiders-left counter, big phase banners, the seeker blindfold, and name
tags projected with `mDoLib_project`.

## Needs checking in game

These can only be verified with the game running; see [testing.md](testing.md).

See [testing.md](testing.md). The riskiest parts: the animation blending on the puppets, warps into
every map, holding hunters still with a post-hook on `daAlink_c::execute`, the sword hit cylinder,
and the game mode's save setup.

## Later

- Join by address (LAN/Tailscale) on NetService, no server.
- Wolf form puppet (`Wmdl`), other items in hand.
- Custom spawn points per map via StageService (Dusklight lets mods add `Link` points with ids > 255),
  and separate hunter/prop spawns.
- Spectator camera for found players.
- Props that break or move in one game (a hunter smashing a real pot) aren't synced; a lone pot where
  one was smashed is a giveaway. Syncing pot breaks, or making real pots unbreakable during rounds.
- Prop rotation lock, prop health (big props take two hits), a "whistle" hint for hunters.
