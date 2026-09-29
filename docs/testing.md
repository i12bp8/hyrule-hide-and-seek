# Checking it in the game

What's been verified without the game:

- Everything compiles for Linux with GCC and Clang against Dusklight v2.0.2 (the other platforms build in CI).
- The rules (`tests/run.sh`): rounds, hunter rotation, gather timeout, hit validation, tag immunity,
  live scoring, bounded/paid decoys, late joiners, host leaving mid-round, unique colours.
- The relay (`server/`, `npm test`): on Node and on the actual Cloudflare Worker via `wrangler dev`.
- The test bots speak the game's wire format.
- The tunic recolour on Link's real textures (`tests/recolor_preview.py`).
- Every map's spawn point exists in the stage files, standing, with no event attached.

Everything below needs the game running. Go in order: later steps depend on earlier ones. Keep
Dusklight's log open (`~/.local/share/TwilitRealm/Dusklight/logs/`); the mod logs as
`com.i12bp8.hyrule_hide_and_seek`.

## Setup

```sh
cd server && npm install && node node-server.mjs
```

In the game: Mods → enable Hyrule Hide & Seek. Menu bar → Hide & Seek → Settings → **Use a server
on this computer**.

## 1. Loads and connects

- [ ] Log shows `Hyrule Hide & Seek 0.1.11 ready`. No "hook ... did not resolve" warnings for
      `daAlink_c::execute`, `daAlink_c::draw`, `daAlink_c::setCutType`,
      `daAlink_c::setDamagePoint`, `daAlink_c::checkNotBattleStage`, `fopAc_Execute`, or
      `dComIfGp_event_order`.
- [ ] On the official Linux 2.0.2 build, log shows `using the HTTP relay fallback`; the mod is
      active rather than failing on a missing WebSocket service.
- [ ] Hide & Seek tab opens the window with Play / Rules / How to play / Settings.
- [ ] Play cleanly separates **Host a game**, **Join a game**, and (once connected) **Current room**.
- [ ] **Host a game** → **Create room and copy code** gives a toast, copies the code, and the HUD
      shows `Room ABCDE` at the top.

## 2. Other players are drawn

`node bots.mjs --room ABCDE --count 3`

- [ ] Three complete Links appear around you, each a different tunic colour, standing (idle animation).
- [ ] Walk away: they walk after you, feet on the ground, shadows under them. Their body never
      flickers, disappears, T-poses, or leaves only a face/sword visible.
- [ ] Name tags above them in their colours.
- [ ] Your own Link keeps animating normally (the puppets must not disturb it).
- [ ] Log: `linkkit: Link's files ready`, `linkkit: <colour> tunic loaded`, heap size message.
- [ ] Walk through a door: the bots follow a moment later in the new area.

If Links show up in a T-pose or explode into spikes, the animation blending is wrong: note which
animation (standing or running) and send the log.

## 3. Your own colour

- [ ] Rules don't matter here: pick a colour in Play → Tunic colour. Your own tunic changes within a
      second (Hero's Clothes only). Other players see you in that colour.

## 4. A Prop Hunt round (you as a prop)

Start round. With 4 players (you + 3 bots) there is one hunter; restart until you're a prop.

- [ ] Everyone is warped to the map (Random picks one; try a fixed map first: Ordon Village).
- [ ] "Get ready" → then `HIDE!` banner and a countdown.
- [ ] You turn into a pot (Link disappears, a pot is drawn where he stands, with a shadow).
- [ ] D-pad right next to a real pot/crate/barrel: you copy it. Away from one: next prop. D-pad left: previous.
- [ ] The replacement is visible from the first Hide frame and after changing props; Link never
      disappears before its prop model is ready.
- [ ] D-pad left/right/up/down performs only the Hide & Seek action. The vanilla Items and Map menus
      do not open underneath it.
- [ ] During Hide, D-pad up places five free copies of the current disguise at your position. The HUD
      counts down 5 → 0; they remain fixed after you move away, are at least a prop-width apart, and
      placing a sixth free one fails cleanly.
- [ ] Set **Free decoys per hider** to 0 and 10 in separate rounds. Zero starts with none; ten grants
      ten placement charges and retains all ten from that hider. With eight players, each hider
      can retain ten without evicting anyone else's decoys; the structural room capacity is 160.
- [ ] The cucco prop animates when you walk.
- [ ] Cycling props stays within a varied, map-appropriate pool; test at least one village, water,
      forest, mountain and desert map.
- [ ] Cycle all 54 selectable catalogue entries across those maps. Every choice draws a model; a bad
      optional asset logs a warning and shows the fallback pot instead of making the hider invisible.
- [ ] Laundry is roughly player-height and rests on the floor instead of appearing tiny/underground.
      Crystal appears near the player at a readable size instead of at its authored world origin.
- [ ] The hunter bot waits at the spawn during Hide, then chases you during the hunt and tags you:
      "You were found!", you become Link again (and a hunter).
- [ ] D-pad down taunts: a Link shout plays, the taunt bar fills, and the hunter gets a five-second
      direction/distance clue plus a marker at your position.
- [ ] Automatic last-minute taunts start disabled. Enabling the host rule makes them occur; turning
      it off leaves manual and stationary taunts only.
- [ ] The stationary auto-taunt defaults to 60 seconds. Moving more than a small step resets its
      timer; Off disables it, and a shorter setting fires at the selected delay.
- [ ] Cucco, pumpkin and oil jar have one clean circular shadow with a sensible footprint. Static
      rocks/furniture, targets and flat/translucent plants have no added black blob or duplicate mesh.

## 5. A round as the hunter

Restart until you're the hunter (1 in 4).

- [ ] Hide phase: black screen with a countdown, you can't walk away from the spawn.
- [ ] The bots are props standing near you (pots, crates...). No name tags over them.
- [ ] The mod does not add fake scenery props to the map; only real hiders are drawn as props.
- [ ] Player-placed decoys are visible during Hide and Hunt but remain walk-through. Hitting one
      removes it, does not award 5 find points, and is treated as a missed swing.
- [ ] Sword swing (B) hitting a bot prop: spark, "Found Bot N!", the counter at the top drops.
- [ ] Walking into a hider never pushes, snags or teleports Link; the same prop still registers a
      sword hit throughout its visible footprint.
- [ ] While swimming, get close to a prop and press B: the prop is found even though Link cannot use
      his sword. B keeps its normal swimming behaviour, and distant props are not tagged.
- [ ] Castle Town still allows drawing and swinging the sword during the hunt.
- [ ] From a hider's client, the hunter's complete Link body remains visible while idle, running and
      swinging throughout Hide, Hunt and Results—never only a face/sword, a T-pose or a name tag.
- [ ] Swinging at nothing costs one full heart when possible (never the last quarter). Hitting a real
      map pot or a player decoy also costs one; hitting the actual hider does not.
- [ ] After surviving 30 seconds, the hider HUD shows 3 live round points. With all free placements
      used, D-pad up buys one extra decoy, replaces the oldest owned one, and reduces both round and
      total score by 3. Paid decoys cannot be placed during Hide.
- [ ] Hitting every prop ends the round: "HUNTERS WIN!", the scoreboard, then a new round.

## 6. Hide & Seek mode

Rules → Mode → Hide & Seek.

- [ ] Hiders stay Link. As the hunter, touching a hider finds them.
- [ ] Hider name tags only show up when you're close.

## 7. Two real players

With a friend (or two copies of Dusklight with different `--mods` folders and profiles):

- [ ] Both see each other animated in the right colours, including sword swings.
- [ ] Sword hits between real players register.
- [ ] The host leaving mid-round: the other player becomes host and the game carries on.

## 8. Game mode

- [ ] Title screen shows a **Hide & Seek** game mode with its own save files.
- [ ] A new file skips the story: you land in Ordon Village in the Hero's Clothes with a sword,
      shield and six hearts, and the Hide & Seek window opens.
- [ ] Faron, Kakariko and Lake Hylia have no twilight.
- [ ] Ordon Village, Kakariko, Lake Hylia, Sacred Grove and Hidden Village load directly into
      normal play: no story camera, forced dialogue, tutorial, minigame, Postman or item popup.
- [ ] The world stays at noon and Link cannot turn into a wolf.
- [ ] NPCs, chests and story triggers cannot start dialogue, item-get scenes or missions.
- [ ] No enemies or bosses remain, including ones spawned by another mod; no encounter-complete
      cutscene or timer starts when they disappear.
- [ ] Damage does not remove hearts and underwater air stays full. A hunter's missed sword swing
      still removes one heart (without taking the last quarter) when that rule is enabled.
- [ ] Leave through a loading zone after the round begins: the client returns to the selected map's
      spawn and rejoins the round.

## 9. Every map

Rules → Map → each one in turn (Start round, then End round now):

| Map | Warp works | Nothing odd (enemies, cutscene) |
| --- | --- | --- |
| Ordon Village | | |
| Ordon Ranch | | |
| Ordon Spring | | |
| South Faron Woods | | |
| Kakariko Village | | |
| Kakariko Graveyard | | |
| Death Mountain Trail | | |
| Zora's Domain | | |
| Upper Zora's River | | |
| Lake Hylia | | |
| Castle Town | | |
| Sacred Grove | | |
| Hidden Village | | |
| Gerudo Desert | | |
| Hyrule Field | | |

Drop maps that misbehave from `src/maps.cpp`.

## 10. Online

- [ ] Deploy the relay (server/README.md), set the address in Settings (or build with it), host and
      join from two different networks.
- [ ] Rules → List this room publicly: it shows up in Play → Public games on the other machine.

## Stock-render regression (developers)

The v0.1.10 lobby/decoy abort was caused by duplicate model submissions forming cyclic retained
draw lists. Models must calculate in execute, enter through `mDoExt_modelEntryDL`, and belong to
the stage layer. Increasing Dusklight's GPU buffers does not fix that cycle.

Build the opt-in integration driver separately (never distribute this test bundle):

```sh
cmake -B build-stock-render -DHS_STOCK_RENDER_TEST=ON
cmake --build build-stock-render --parallel
```

Use the default secure relay, or configure another HTTPS relay (stock Linux's HttpService rejects
plain HTTP, including loopback). Copy the test bundle into an isolated user directory's `mods`
folder, configure its disc path, and copy a playable save into its `USA/Card A` folder. Launch the
original, unmodified Dusklight with `--user-dir <isolated-dir> --mods <isolated-dir>/mods
--load-save 1 --stage F_SP103,0,13,-1`. Enable frame interpolation and keep the window focused
(or disable pause-on-focus-loss in the isolated profile).

The driver populates 16 player states, starts a round, cycles all 54 selectable props with 160
decoy snapshots, then leaves and checks cleanup. It checks the actual material/shape lists for
cycles before rendering and exits successfully only after logging `STOCK_RENDER_TEST PASS`.
Injected states exercise the real actors and rendering, not 16 independent network clients.

- [ ] Run on stock Linux and Windows, both with interpolation on and off.
- [ ] Separately join real cross-platform clients and place decoys with D-pad up.
- [ ] Repeat rounds, leave/rejoin, change stage, and verify complete animated Link bodies.
