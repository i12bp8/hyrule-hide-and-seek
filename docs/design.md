# Hyrule Hide & Seek: v0.4.1 design

Online Prop Hunt for Twilight Princess on Dusklight. A room has 2–16 players, a host that runs
match rules, and a relay that forwards messages. Players install one `.dusk` bundle, host a room
and share its five-letter code. The classic Link-only Hide & Seek mode has been removed.

## Round flow

1. Lobby: coloured Links meet; the host picks a map or Random and round settings.
2. Gather: players warp to the arena and report ready, with a bounded loading timeout.
3. Hide: props choose a disguise and place decoys; hunters remain at spawn behind a black screen.
4. Hunt: hunters find every prop with sword hits or nearby swimming tags before time expires.
   Found props become hunters if infection is enabled; eliminated hunters spectate.
5. Results: the scoreboard shows round/session points and style statistics. Automatic rounds
   rotate starting hunters toward players who have hunted least.

Default timing is 30 s Hide, 180 s Hunt and 12 s Results. Score budgets and search pressure are
specified in [balance.md](balance.md).

## Arenas

`tools/arena_builder.py` derives standing spawns, actor fingerprints and extra native scenery
from the user's extracted disc. Its checked-in outputs are `src/arena_data.inc` and
`server/arenas.json`. The generated file includes only coordinates, actor parameters and hashes;
models, textures and animations remain in the player's own game files.

The 11 maps are played whole (every room the game loads with the spawn room; loading exits closed,
no invisible fence), with themed disguise palettes and selected post-story layers. Scripted actor records are filtered before creation; native clutter is added
through the optional StageService. Arena hooks stop exits, scene changes and compulsory events,
keep the field camera free, and prevent void transitions. Interaction attention is cleared during
rounds so real pots, pumpkins and grass do not expose disguises through A/Z prompts.

The separate title-screen **Prop Hunt** game mode prepares a completed-story sandbox save, fixed
daylight, sword/shield, five hearts and underwater air. Its save identity is unchanged, preserving
existing dedicated saves. Normal story saves can also join; their original life capacity is
restored on leaving. Arena protections apply during online rounds without rewriting story saves.

## Disguises and decoys

There are 75 selectable disguises plus a renderer-only treasure rupee. The catalogue records each
native model, secondary model, resource entry/name, idle/walk animation, material animations,
light type, draw list, shadow type/size/lift, scale, offset and collision. Map palettes constrain
normal selection; copying a nearby native object uses the same catalogue.

- Plants use native wind and proximity sway. Lily pads query the water surface and bob on nearby movement.
- Animals and townspeople use native idle/walk animations. Citizens share Mgeneral/Wgeneral animation archives.
- Oil jars draw both the jar and animated oil surface.
- Shadows match native round, square or projected-model shadows; objects without native shadows add none.
- Solid objects use native push cylinders or archive collision meshes. Meshes near local Link are
  registered in the game's fixed table; distant meshes release their slots. Capacity failures
  temporarily use a push cylinder and retry, rather than permanently disabling collision.
- A mesh never closes around the player who placed it. The player's own disguise does not block them.
- Sword target cylinders cover visible props, including floating pads above swimming players.

Three free decoys are recommended; the host can choose 0–10. Further placements during Hunt cost
3 points. Each player retains up to ten decoys; an eleventh replaces their own oldest, with a
structural room capacity of 160. Hunter hits remove decoys and count as misses. During Hunt a
prop can swap with their newest decoy twice, at least 15 s apart. Up taps place decoys; a 0.45 s
hold requests a swap.

## Multiplayer and authority

Each client makes an outgoing WebSocket connection. The optional HTTPS fallback supports older
private Node relays. Public rooms are listed by the relay; the host remains authoritative for
roles, timers, scores, health, treasure and decoys. The relay enforces matching protocol versions.
A modified host/client is not made trustworthy by this protocol.

Requests carry a round id, target or pickup id where needed. The host derives positions from
fresh player states and checks role, stage, distance, map, phase, cooldown, allowance and points.
Snapshots support late joins and host migration. Remaining ability/style cooldowns are encoded
as durations rather than absolute clocks. Roster parsing is atomic: malformed or duplicate rows
do not partially mutate the match.

### Protocol 11

Frames sent to the relay start with `u8 to` (0 broadcast, 255 host, 1–16 player); received frames
start with `u8 from`. JSON control frames describe joins, departures and host changes. Binary
payloads use little-endian numbers, as defined in `src/protocol.hpp`.

| Message | Content |
| --- | --- |
| STATE | Animated Link state (70 bytes), or compact prop/loading state (28), including type |
| SETTINGS | Map, times and options; no mode field |
| ROSTER | Count plus 28-byte player rows: roles, colour, scores, counters, health, style stats and three remaining timers |
| ROUND / PHASE | Round id, map, times, starting prop count/capture progress; phase and remaining time |
| READY / HIT / FOUND / RESULTS | Loading acknowledgement, find request, confirmed find and winner |
| DECOYS / PLACE_DECOY / HIT_DECOY | Complete bounded snapshot, placement request and strike request |
| SWAP / TELEPORT | Swap request and host-confirmed position/yaw |
| AWARD | Player, award type and granted points |
| RUPEES / COLLECT_RUPEE / PICKUP | Treasure snapshot, validated claim and score/challenge feedback |
| TAUNT / CLUE | Sound request; manual, final, stationary or treasure clue |
| MISS / HUNTER_OUT | Cumulative miss count and confirmed hunter elimination |

Protocol 11 changes catalogue/map IDs and payloads and cannot join v0.3.x rooms. Existing saved
rules migrate to format 6; old map choices reset to Random while other custom rules survive.
The relay does not need an upgrade for the new game payloads; bots do.

## Rendering and memory

A custom stage-layer actor per remote player owns its models and animation state. Coloured Link
body/cap models are copied from Kmdl into a 64 MiB heap backed by host memory. The private JKR heap
is registered in the game heap tree, supports allocator lookup, and avoids consuming the fixed
root heap. Native warp texture stages are disabled and display lists regenerated.

Link animations are read by ordinal from immutable AlAnm archive bytes in that same heap, using
bounds-checked RARC parsing. The archive is not mounted a second time: a second mounted index table
used the small System heap and caused an allocation abort when loading Telma's Bar. Animation
copies retain immutable key data but have independent frame values and upper/lower body blends.
Unknown/demo entries fall back to valid idle/walk animation.

Prop resources use the native resource manager. Joint callbacks/calculators and material
animations on shared model data are saved, temporarily replaced for the puppet and restored.
Prop heaps shrink after loading. Animation/geometry calc happens on simulation ticks; drawing uses
`mDoExt_modelEntryDL` so presentation frames reuse retained packets without duplicate submissions.
Integration checks walk real material/shape lists for cycles; larger GPU buffers cannot fix a
cyclic packet list.

Movement uses a 100 ms snapshot buffer, short-angle yaw interpolation, up to 100 ms extrapolation
and snapping for large teleports. State sends adapt between 1–10 Hz. Confirmed swaps clear the
old interpolation sample. Decoys whose snapshot positions change respawn at the new location.

## Treasure and interface

The host explores connected native collision from spawn and visited routes, with bounded probes
per frame. Floor/slope, wall, cliff, footprint, water and exit checks reject candidates. Spawning
favors separated candidates, checks them again and respects removed-location cooldowns. Unloaded
rooms cannot be explored until their collision loads. Tight interiors can contain fewer pickups.

The native theme and fonts remain. Menus use large controls and stack on narrow screens. Central
status cards keep the sides available for touch controls; name labels reserve screen rectangles.
Hunters do not see prop names. At most two nearby treasure labels are drawn. Clues use a summary
and live bearing arrow; only the final reveal adds a precise marker. Results fit 16 players and
include style feedback. Mobile touch and safe-area behavior require actual device checks.

## Developer tools and limits

`tests/run.sh` covers rules, protocol, settings, interpolation, layout and RARC bounds. The Node
suite covers relay/bot interoperability. The stock game driver exercises rendering, memory failure
recovery and arena protections. `tools/lab` provides an isolated, opt-in game harness;
`tools/showcase` provides opt-in staged filming. Neither is enabled in release bundles.

Linux loading and stress checks do not establish balanced win rates or validate every map route,
mobile device or high-latency cross-platform session. Native NPC movement and object breakage are
not synchronized. The host can leave and transfer authority, but a severed connection does not
reconnect automatically. See [testing.md](testing.md) for recorded evidence and remaining manual checks.
