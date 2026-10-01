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
3. **Hide** (default 30 s). Props scatter and pick a disguise. Hunters stand at spawn with a black
   screen and a countdown.
4. **Hunt** (default 180 s). Hunters have to hit every prop with their sword before the timer runs
   out. A found prop becomes a hunter (default) or watches.
5. **Results** (12 s). Scoreboard, then the next round starts by itself with new hunters (the
   players who have hunted least go first).

### Prop Hunt

- Props: 57 selectable objects drawn from every region, with map-themed selection. Models are read
  from the player's own game files. Nine legacy IDs remain reserved for protocol compatibility but
  are not offered because their native actors require multiple models, particles or environment
  placement that a standalone disguise cannot reproduce safely.
- D-pad right copies the pot/crate/barrel you stand next to, like "become the prop you look at" in
  Garry's Mod; with nothing nearby it cycles. D-pad left goes back.
- D-pad up places a copy of the current disguise. The default allowance is three free placements per
  hider and the host can choose 0–10. Once used, extra placements are available during Hunt for 3
  points earned in that round. Every player can keep ten active; an eleventh replaces only that
  player's oldest, never somebody else's setup. The room and wire format retain up to 160 decoys,
  enough for every slot's full allowance. Placements have a short cooldown and minimum spacing, are
  as solid as the object they copy, give no hunter points, and count as a miss when struck. A struck
  decoy vanishes for the hunter at once and for everyone when the host confirms, so hiders can wall
  off a passage and hunters can cut through it.
- Hunters hit props with a real sword swing. Each hider has a target-only cylinder for Link's sword
  (and wolf attacks). Disguises and decoys also copy the native actor's collision (props.cpp
  `kSolids`): a push cylinder like pots and rocks (Co 0x79), or the archive's own .dzb collision
  mesh like furniture, fences and chests; grass, laundry and the other walk-through objects stay
  walk-through. A mesh only becomes solid once the local Link is clear of it, so a decoy never traps
  the hider who placed it. Your own disguise never blocks you. While swimming, where the game
  prevents sword use, B performs a close-range prop tag. A sword swing that hits no real hider costs
  half a heart by default (host-adjustable from Off to one heart in quarter-heart steps; never
  the last quarter). Every round starts with five hearts. Hunters can sheathe their sword normally.
- D-pad down requests a host-confirmed taunt for a point (once per 10 seconds). Hunters hear a
  Link shout and get a broad direction sector and range band for three seconds, with a four-second
  cooldown. There is no exact distance or world marker. Clue categories are captured relative to
  the hunter's view when the clue arrives and stay fixed. The HUD draws a matching direction
  arrow. Treasure uses the same rough clue. Stationary clues default to Off.
  There is just one exact world marker per surviving hider for three seconds at 20 seconds
  remaining, adjustable from 0–60 (0 disables it), capped at half the hunt for short rounds.
  The finale bypasses the ordinary clue cooldown to fire on time. Its exact location is captured
  once; a later pickup cannot overwrite its marker or reveal alert. The roster carries whether the finale
  has fired, so host changes and late joins cannot repeat it. The hider sees a clue alert and
  upcoming-clue countdown. Hunters get a three-second direction/range pulse every 25 seconds.
- Hunters don't see props' name tags.

### Hide & Seek

Everyone stays Link; hunters tag hiders by touching them (SMO Online rules). Name tags remain
visible at the same range as the player models, including hider names seen by hunters in this mode.

### Scoring

- Props: up to 12 live points in proportion to hunt time survived, +6 for surviving, and up to 6
  bonus points from loot/manual taunts. Taunts give 1 at most every 10 seconds.
- Treasure: +1 per pickup (+2 every third round), with up to +2 at the third pickup. Loot and
  taunts share the same gross bonus limit; spending cannot reset it. Pickups publish clues.
- Starting hunters: up to 12 shared points in proportion to confirmed captures of starting hiders,
  +6 for winning; personal finds give 3 bonus points (6 if only one hider started), within the same
  6-point bonus limit. Infection gives no second objective/win award. Finds restore one heart;
  exhausted misses impose two seconds of recovery.
- After the free allowance, an extra decoy spends 3 current-round points.
- Totals are kept for the room until the host resets them.

Both roles have a 24-point ceiling regardless of duration or lobby size. See
[balance notes](balance.md) for the primary references, rationale and remaining playtests.

### Maps (spawn points verified against the disc files)

Ordon Village, Ordon Ranch, Ordon Spring, South Faron Woods, Kakariko Village, Kakariko Graveyard,
Death Mountain Trail, Zora's Domain, Upper Zora's River, Lake Hylia, Castle Town, Sacred Grove,
Hidden Village, Gerudo Desert, Hyrule Field, or Random every round. A missing spawn point crashes the
game (`dStage_playerInit` logs a fatal error), so each map uses a point that `tools/find_spawns.py`
lists as standing with no event. A player is only visible to others in the same stage, so the round
keeps everyone inside it: scripted doors/events are inert, loading-zone floors and exit volumes
block movement, and native scene transitions cannot reload or leave the selected stage. The
normal free third-person field camera overrides authored fixed-camera tools on every arena.

**World state.** Areas look different depending on story progress (twilight, NPCs). The
**Hide & Seek game mode** on the title screen uses its own completed-story save profile. Persistent
story, dungeon, side-quest, tutorial and forced-conversation flags select the calm late-game layers;
twilight and transformations are disabled; valid Postman letters are already delivered; first-time
rupee messages are cleared; and the clock stays at noon. Players get the Hero's Clothes, sword,
shield and five hearts, so every player's world matches and every hunter has a sword. Scripted event
orders are rejected, active events are reset, and enemy/boss actors plus encounter controllers are
removed before they can execute. Damage and drowning cannot end play (the optional hunter miss
penalty still can remove hearts). The live sandbox protections also apply during an online round
started from a normal save, but only the dedicated game mode applies the matching completed-story
save profile.

## v0.3.0 review and changes

| Finding in the previous rules | Change | Effect to assess in playtests |
| --- | --- | --- |
| Staying hidden is usually better than moving. | Reachable random treasure, eight at most, expires after 30 seconds; collecting publishes a three-second clue. | Gives hiders a choice between safety, score and decoy funding. |
| Five free decoys postpone the point economy. | Recommended allowance reduced to three; extras remain three points. | Early placements still matter, then loot and manual taunts fund more. |
| Automatic taunts were client scheduled and hard to notice locally. | Host schedules and confirms them; hider banner names the reason and counts down the reveal. | Everyone agrees a clue occurred; tiny movement cannot avoid stationary clues. |
| Hunters can spam forever at the final quarter heart. | Two-second recovery after exhausted misses; confirmed finds restore one heart. | Makes misses matter while retaining the native five-heart round setup. |
| Final moments with one hider can drag. | Eight-second clues for the last hider in the final minute. | Adds a readable endgame without continuously exposing a position. |
| Repeated rounds have no optional side objective. | Three-pickup challenge each round and Treasure Rush every third round. | Introduces a score objective and periodic rule variation without persistent grinding. |
| Regional prop pools lack recognisable friendly characters. | Ordon goat, village cat, Castle Town citizen/shopper and settlement containers. | New disguises use the disc's native models and idle/walk animations. |
| Linux needs a different transport and idle state costs quota. | Pin official Dusklight 2.0.3, adaptive sends, compact prop states, bounded/coalesced transport. | Lower traffic and smoother moving players on the common WebSocket path. |
| Lobby metadata exists only in memory. | Indexed SQLite listing in a hibernating Durable Object. | Public rooms survive Lobby reconstruction. |

### Interface for mobile and crowded rounds

The original theme colours and font families are retained. Menus use scoped RCSS, 46 dp minimum control targets, and a single-column layout below 640 dp.
The game HUD uses the middle lane so the native touch controls can occupy the sides. Timer and
role cards replace long persistent instructions. Control hints start on and can be disabled. World labels reserve
screen rectangles: all visible players can have name tags, alongside at most two treasure labels
and three compact clue markers; intersecting labels are skipped. Name tags have no separate
distance cutoff and require a loaded, visible puppet. One clue summary replaces stacked paragraphs. Reveal
feedback names its cause and shows remaining time, with a restrained amber pulse. Results compute
row height from available space and keep all 16 rows inside the viewport. Mobile hardware must
still be used to verify touch dispatch and device safe areas.

### Treasure implementation

Only the host explores connected ground, starting during Hide at the safe spawn and visited
player positions. Ground, slope, body-height line, intermediate-floor and footprint probes reject
missing floors, height changes, walls, deep water and loading exits. Exploration retains distant candidates
after players move away and is bounded per frame. Spawning favors the farthest candidates from
existing pickups, checks them again against native collision and enforces 1,200-unit horizontal
spacing. Collected and expired locations retain that spacing exclusion for 30 seconds; new
pickups also stay 600 units from fresh, active hider positions. The candidate search skips these
blocked locations rather than repeatedly choosing the same empty spot. Every client records
snapshot removals, preserving cooldowns across host changes without changing the protocol.
Cooldowns reset for a new round or room. This is collision-based exploration, not verified hand-placed coordinates. Tight spaces
can have fewer candidates; unloaded rooms need their collision to load before exploration.

The host accepts pickup requests only from live hiders in Hunt with a fresh state on the round's
stage, the current round id, an unexpired pickup and a nearby position. It consumes the pickup
before announcing points, preventing duplicate awards. Clients draw at most 24 lightweight,
non-solid rupee actors using the native rupee model and colour animation. New rounds/results clear
them. Pickups live 90 seconds. Late joiners and a migrated host receive the remaining snapshot.

### Limits and further evaluation

The relay still trusts the host and cannot make a modified client honest. There is no account,
matchmaking rank, persistent unlock grind or automatic recovery from a severed network connection.
Friendly native animals and people are preserved, and new disguises are selectable; this release
does not add a general synchronized NPC population. Native pot breakage still differs between
clients. Real cross-platform playtesting must assess animation, disguise scale, pickup reachability,
map balance, and latency. Regional prop masks help variety; map size and routes still determine
how useful hiding spots are.

Cloudflare can run many independent lobbies, but its Free quotas bound daily usage. A local
200-player throughput test does not prove indefinite free hosting or worldwide smoothness. See
[server capacity](../server/README.md#free-tier-capacity) and [checks](testing.md).

## Architecture

```
┌─────────── game (C++ mod) ───────────┐            ┌──── relay (Worker / Node) ────┐
│ ui.cpp      window: host/join/lobby   │            │ rooms: code → players (≤16)   │
│ match.cpp   rules, rounds, roles      │  wss://    │ forwards [to][payload]        │
│ net.cpp     WebSocket, bounded queues      │◀──────────▶│ as [from][payload]            │
│ local.cpp   read Link, warp, freeze   │            │ JSON control: welcome, join,  │
│ puppet.cpp  draw other players/props  │            │ leave, host changes           │
│ hud.cpp     timer, banners, name tags │            │ /rooms: public room list      │
└───────────────────────────────────────┘            └───────────────────────────────┘
```

### Authority

The room's host (first player, or the next lowest id if the host leaves) is authoritative for the
round: roles, phase timers, tags, scores and decoys. Placement requests contain no client-supplied
model or coordinates: the host uses that player's latest state, verifies role, phase, disguise, map,
freshness, spacing, cooldown and points, then broadcasts the complete bounded snapshot. Hit claims
are checked against hunter role, stage and distance. Every message the host sends is also applied
locally, so the host's own game follows the same code path.

### Wire protocol

Relay framing (binary WebSocket frames):

- client → relay: `u8 to` (0 = everyone else, 255 = host, 1-16 = one player) + payload
- relay → client: `u8 from` + payload

Control messages are JSON text frames (see `server/src/room.js`).

Game payloads start with a `u8` type; all numbers little-endian (`src/protocol.hpp`):

| Type | Dir | Content |
| --- | --- | --- |
| `STATE` | all, adaptive 1–10 Hz | full animated Link state (70 bytes) or compact prop/loading state (28 bytes), including type |
| `SETTINGS` | host → all | mode, map, times, options |
| `ROSTER` | host → all | per player: current/starting role, colour, scores, decoy allowance, found flag, loot/find counts, gross bonus and objective counters |
| `ROUND` | host → all | round id, map, times, starting hider count and capture progress |
| `PHASE` | host → all | phase + milliseconds left |
| `HELLO` | → host | wanted tunic colour |
| `READY` | → host | "I've loaded into round N" |
| `HIT` | → host | "my sword hit / I touched player N" |
| `FOUND` | host → all | round, target, hunter and capture progress |
| `RESULTS` | host → all | winner |
| `DECOYS` | host → all | complete active-decoy snapshot (maximum 160) |
| `TAUNT` | → host | round id, sound request |
| `CLUE` | host → all | round id, hider, sound, manual/regular/stationary/treasure reason |
| `RUPEES` | host → all | round id, up to 24 pickups with id, position, remaining lifetime |
| `COLLECT_RUPEE` | → host | round id and pickup id; no client coordinates or score |
| `PICKUP` | host → all | collector, points and challenge-completion feedback |
| `PLACE_DECOY` | → host | placement request for the current round |
| `HIT_DECOY` | → host | decoy id struck by a hunter |

### Drawing other players

A custom actor (`HSPupt`, registered through ActorService) per remote player in the same stage:

- Link: the Hero's Clothes body, cap, face and hands, copied once out of `Kmdl.arc` (plus sword,
  sheath and Hylian Shield) into the mod's own heap. The body and cap are loaded once per tunic
  colour with the green shifted in the CMPR texture data (`src/recolor.cpp`), so every player looks
  different and none of it is shared with the local Link.
- Animation: the native three-slot lower/upper blends from each player's network state. BCKs
  come from a permanent private `AlAnm.arc` mount and are accepted as concrete key transforms
  (kind 8). Each puppet slot copies the immutable key-data pointers and owns its frame; lower
  body calculators drive joints 0/16 and upper body joint 1. Unknown/demo animations use an
  animated idle/walk fallback. Shared model calculators are restored after simulation calc.
- Props: the prop's archive model through the resource manager, drawn at the player's position;
  joint callbacks and animations the real objects put on the shared model data are swapped out
  around our `calc()`.
- Position uses a 100 ms snapshot buffer, short-angle yaw interpolation and at most 100 ms of extrapolation; teleports over 600 units snap. Moving players send at 10 Hz, stationary players at 2 Hz during rounds and 1 Hz in the lobby. Player decoys use the same prop renderer but stay
  fixed at host-approved snapshot positions. Carryables and a few movable actors use cheap
  native-sized simple shadows; static or flat/translucent scenery casts no extra dynamic blob.
  Disguises never use model-projected shadows, which would submit complex geometry again and can
  consume older Dusklight builds' fixed per-frame index buffer unnecessarily. Models calculate on
  simulation ticks and use `modelEntryDL` for drawing, so presentation frames reuse retained packets
  without inserting them again. Puppets belong to the stage layer, avoiding a second submission by
  the root-layer iterator. Re-entering a packet can make a cyclic list that draws forever; a larger
  GPU buffer only delays that abort. No patched Dusklight is required.
- Link's privately loaded BMWR models have their warp texture stages disabled, as native Link
  does during model initialization. Their shared display lists are regenerated afterward;
  leaving the warp stage enabled can discard the entire body while its name tag remains visible.
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
