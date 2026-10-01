# Checking it in the game

What's been verified without the game:

- v0.3.1 compiles for Linux with GCC against Dusklight v2.0.3; other platforms require CI builds and runtime checks.
- The rules (`tests/run.sh`): rounds, hunter rotation, gather timeout, hit validation, tag immunity,
  live scoring, bounded/paid decoys, late joiners, host leaving mid-round, unique colours.
- The relay (`server/`, `npm test`): on Node and on the actual Cloudflare Worker via `wrangler dev`.
- The test bots speak the game's wire format.
- The tunic recolour on Link's real textures (`tests/recolor_preview.py`).
- Every map's spawn point exists in the stage files, standing, with no event attached.

Everything below needs the game running. Go in order: later steps depend on earlier ones. Keep
Dusklight's log open (`~/.local/share/TwilitRealm/Dusklight/logs/`); the mod logs as
`com.i12bp8.hyrule_hide_and_seek`.

## v0.3.1 balance and distribution checks

- [ ] On each of the 15 maps, leave players near spawn during Hide, then explore the map during
      Hunt. Rupees appear well beyond spawn, including routes around corners. No dense clusters,
      unreachable cliffs, steep surfaces, walls, deep underwater floors or loading exits.
- [ ] On broad maps, up to 24 rupees appear, 1,200 units apart. Tight maps support fewer. Rupees
      expire at 90 seconds and refill in separated areas. Check frame time during exploration.
- [ ] Walk between rooms and up stairs/ladders: visited reachable ground expands the candidate
      area; unloaded floors do not get pickups. Verify narrow routes do not leave a region empty.
- [ ] Control hints start on in a fresh installation and once after upgrading. Switch Off and
      restart; the choice persists. Test both modes on desktop and mobile.
- [ ] Both roles: up to 12 objective, 6 gross bonus, 6 win points. Test 1v1 and 4/8/16-player
      rounds. Captures advance every starting hunter's progress, including finds by infected hiders.
- [ ] Found hiders keep survival points and can help hunt, with no second objective/win award.
      Late joiners get no starting-team progress/win bonus until the next round.
- [ ] Normal loot gives 1, third pickup adds up to 2; Treasure Rush gives 2. Loot and manual taunts
      stop scoring at 6 bonus points. Spend 3 on a decoy, collect again: the cap stays exhausted.
- [ ] Change host mid-hunt after a find and after spending points. Shared progress, gross bonus
      limits and already awarded survival survive without duplicate points.
- [ ] Stock stationary clues are Off. Each remaining hider gives one final clue at 20 seconds left,
      with no recurring/accelerated last-hider clues. Final clue 0 disables it; custom Off and
      stationary timings survive the stock-rule upgrade. Short hunts keep their first half quiet.
- [ ] Run paired sessions with role swaps and record per-map wins, duration, finds and per-player
      points before spending. Assess actual balance using [the playtest notes](balance.md).

## Other gameplay checks

- [ ] In Ordon Ranch, goat disguises idle and walk. In Hidden Village, cats idle and walk.
- [ ] Castle Town citizens and shoppers animate with the shared Mgeneral/Wgeneral archives.
- [ ] Milk jars, baskets and bags match the nearby scenery in scale and placement.
- [ ] Friendly native animals and townspeople remain present; no unwanted scripted dialogue begins.
- [ ] Rupees appear on reachable ground, with no wall/cliff/exit placements, in each of the 15 maps.
- [ ] Hiders collect once, receive 1 point, and complete the three-pickup challenge for up to +2.
- [ ] Round 3 gives 2 points per pickup within the bonus cap; a new round resets loot counts. Treasure Off spawns none.
- [ ] A pickup triggers the local cue and a three-second clue for hunters. The hider sees the reason.
- [ ] Manual and optional stationary clues show CLUE SENT. The final marker shows LOCATION REVEALED
      for three seconds; a subsequent pickup cannot erase that alert or extend the exact reveal.
- [ ] Spinning in place does not reset the stationary timer; moving far enough does.
- [ ] Two hiders reaching the same rupee cannot both receive its points. Expired pickups disappear.
- [ ] Stand on a collected rupee: no replacement appears under the hider, including after 30 seconds.
      Move away: its 1,200-unit area stays excluded for 30 seconds; other areas still refill.
      Expired pickups have the same cooldown. A host change preserves recently removed locations.
- [ ] Miss penalty defaults to half a heart. Test Off, quarter, half, three quarters and one heart;
      decoys use the same cost. At zero the hunter sees empty hearts and OUT OF HEARTS, cannot tag,
      and disappears from other clients until the next round. With two hunters, the first death
      keeps the hunt running; the last immediately awards PROPS WIN. Next round resets life.
      Finding a hider restores one heart up to five. Save/restart preserves the penalty choice.
- [ ] As a hurt hunter, try Ordon spring water, heart drops, fairies and potions: no life is gained.
      Check host/client meters, rapid sword combos and host migration with pending misses.
      Outside an online round/Results, normal story healing and life restoration still work.
- [ ] Rules cannot change until Results. Final clue fires once per remaining hider, including
      after a host change. A recent manual/loot clue does not delay the exact marker.
- [ ] Sheathe the hunter sword and use Horse Grass on South Faron to call Epona. No forced sword
      draw interrupts it. Test normal B combat, including Castle Town, climbing and swimming.
- [ ] Reproduce the reported two-client macOS start: both roles warp to the selected map. Capture
      logs if a hunter stays in Ordon or crashes during warp; that report remains unconfirmed.
- [ ] Compare real clients at 50, 100 and 200 ms latency: walking, corners, teleports, hiding and finding.
      Movement should interpolate smoothly and stop extrapolating after 100 ms of missing updates.
- [ ] Leave the host during Hunt: remaining loot and scores survive the authority change.

### Recorded automated checks (2026-09-30)

- Rules/protocol/interpolation/layout: 4,720 assertions passed, including hunter elimination,
  find-only healing, client prediction and health migration (v0.3.5).
- Node server suite: 16 tests passed. Worker runtime suite: 14 passed, two Node-specific tests skipped.
- Live Cloudflare: health, host/join, sender framing, public listing and host migration passed.
- Official Linux Dusklight 2.0.3 also joined the deployed relay over WSS; a temporary guest
  received the native host's live state packets. No Linux HTTP fallback was used.
- Local Node load: 200 clients / 25 rooms / 10 Hz / 10 seconds; 140,000 expected deliveries received,
  no cross-room messages, p95 ~23 ms. This is a local synthetic test, not worldwide latency.
- Disc asset validation: 67 catalogue entries and 64 unique archives resolved, including disabled IDs
  and the renderer-only rupee. File existence does not establish visual scale or playable balance.

### Mobile and clean interface

- [ ] On Android and iOS, tap Host/Join, edit a code, open dropdowns and scroll public lobbies.
- [ ] In landscape and portrait, the menu stays within the safe area and the navigation stacks
      on narrow screens. Touch targets stay at least 46 dp high.
- [ ] Native touch controls remain available during play, clear of the mod's central status cards.
- [ ] Nearby names do not overlap each other, alerts or the status cards. Hunters never see prop names.
- [ ] At most two nearby treasure point labels appear; gems remain visible without a label.
- [ ] Ordinary clues show one summary with a direction arrow and count, without exact distances
      or world markers. Final reveals alone show precise markers at the reveal locations for
      three seconds; check 1/4/15 hiders, camera turns, moving away, found players and round resets.
      Turning the camera or moving while a clue is visible does not change its sector/range.
- [ ] Results with 16 players stay within the screen; long names truncate before score columns.
- [ ] Control hints default off; the Display toggle shows and hides them cleanly.

Official Linux 2.0.3 completed the real actor sweep in both Hide and Hunt, including all 57
selectable props, 160 decoys and eight rupees during Hunt; no retained draw-list cycles or prop
fallback warnings occurred. This checks rendering and cleanup, not every map's balance or native
mobile touch input.

The native menu was also inspected at a narrow 640 × 720 window: the navigation stacked,
the player list remained visible, and the original theme colours and fonts were retained.
Desktop Dusklight enforces a 640-pixel minimum window width; this is a layout check, not an
Android or iOS device check. Automated HUD bounds cover 320 × 240 through 1280 × 720, including
390 × 844 portrait and all 16 results rows.
The native HUD was inspected during Hunt with an injected manual clue and eight rupees:
the reveal strip named the cause and showed a countdown; only two nearby point labels appeared.

## Setup

```sh
cd server && npm install && node node-server.mjs
```

In the game: Mods → enable Hyrule Hide & Seek. Menu bar → Hide & Seek → Settings → **Use a server
on this computer**.

## 1. Loads and connects

- [ ] Log shows `Hyrule Hide & Seek 0.3.0 ready`. No "hook ... did not resolve" warnings for
      `daAlink_c::execute`, `daAlink_c::draw`, `daAlink_c::setCutType`,
      `daAlink_c::setDamagePoint`, `daAlink_c::checkNotBattleStage`, `fopAc_Execute`, or
      `dComIfGp_event_order`.
- [ ] On the official Linux 2.0.3 build, log shows `Using WebSocket multiplayer on this Dusklight build`.
      On an old Linux build, the public relay tells the player to update Dusklight.
- [ ] Hide & Seek tab opens the window with Play / Rules / Guide / Settings.
- [ ] Play cleanly separates **Host a game**, **Join a game**, and (once connected) **Current room**.
- [ ] **Host a game** → **Create room and copy code** gives a toast, copies the code, and the HUD
      shows `Room ABCDE` at the top.

## 2. Other players are drawn

`node bots.mjs --room ABCDE --count 3`

- [ ] Three complete Links appear around you, each a different tunic colour, standing (idle animation).
- [ ] Walk away: they walk after you, feet on the ground, shadows under them. Their body never
      flickers, disappears, T-poses, or leaves only a face/sword visible.
- [ ] Name tags above them in their colours.
- [ ] Walk beyond 2,500 units: visible player models still have readable name tags. More than four
      separated visible players can have tags; labels still avoid overlaps and status cards.
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
- [ ] During Hide, D-pad up places three free copies of the current disguise at your position. The HUD
      counts down 3 → 0; they remain fixed after you move away, are at least a prop-width apart, and
      placing a fourth free one fails cleanly.
- [ ] Set **Free decoys per hider** to 0 and 10 in separate rounds. Zero starts with none; ten grants
      ten placement charges and retains all ten from that hider. With eight players, each hider
      can retain ten without evicting anyone else's decoys; the structural room capacity is 160.
- [ ] The cucco prop animates when you walk.
- [ ] Cycling props stays within a varied, map-appropriate pool; test at least one village, water,
      forest, mountain and desert map.
- [ ] Cycle all 57 selectable catalogue entries across those maps. Every choice draws a model; a bad
      optional asset logs a warning and shows the fallback pot instead of making the hider invisible.
- [ ] Laundry is roughly player-height and rests on the floor instead of appearing tiny/underground.
      Crystal appears near the player at a readable size instead of at its authored world origin.
- [ ] The hunter bot waits at the spawn during Hide, then chases you during the hunt and tags you:
      "You were found!", you become Link again (and a hunter).
- [ ] D-pad down taunts: a Link shout plays, the taunt bar fills for four seconds, and the hunter gets
      a three-second broad direction arrow/range clue, captured relative to their view when it arrives.
- [ ] One final clue starts enabled at 20 seconds remaining. Turning it Off leaves manual,
      treasure and explicitly enabled stationary clues only. Only the final reveal shows an exact marker.
- [ ] The stationary auto-taunt defaults to Off. Moving more than 120 units horizontally or 80 vertically resets its
      timer; Off disables it, and a shorter setting fires at the selected delay.
- [ ] Cucco, pumpkin and oil jar have one clean circular shadow with a sensible footprint. Static
      rocks/furniture, targets and flat/translucent plants have no added black blob or duplicate mesh.

## 5. A round as the hunter

Restart until you're the hunter (1 in 4).

- [ ] Hide phase: black screen with a countdown, you can't walk away from the spawn.
- [ ] The bots are props standing near you (pots, crates...). No name tags over them.
- [ ] The mod does not add fake scenery props to the map; only real hiders are drawn as props.
- [ ] Stand a disguise next to the real object on the map (crate, pot, gravestone, dresser, fence,
      pumpkin, boar bones): the two are the same size.
- [ ] Player-placed decoys and disguised hiders are as solid as the real object: Link can't walk
      through a crate, pot, fence or table disguise, but can walk through grass or laundry. The hider
      who places a decoy is not trapped inside it and can walk out; it turns solid once they do.
- [ ] Hitting a decoy removes it at once for the hunter and a moment later for everyone, opening the
      passage. It does not award 5 find points, and is treated as a missed swing.
- [ ] Sword swing (B) hitting a bot prop: spark, "Found Bot N!", the counter at the top drops.
- [ ] Walking into a hider never pushes, snags or teleports Link; the same prop still registers a
      sword hit throughout its visible footprint.
- [ ] While swimming, get close to a prop and press B: the prop is found even though Link cannot use
      his sword. B keeps its normal swimming behaviour, and distant props are not tagged.
- [ ] Castle Town still allows drawing and swinging the sword during the hunt.
- [ ] From a hider's client, the hunter's complete Link body remains visible while idle, running and
      swinging throughout Hide, Hunt and Results—never only a face/sword, a T-pose or a name tag.
- [ ] Swinging at nothing costs half a heart by default, including the last quarter. Hitting a real
      map pot or a player decoy uses the configured penalty; hitting the actual hider does not.
- [ ] D-pad down gives a three-second tracking direction/rough range, then shows a 25-second
      cooldown. It never shows the hider's name or an exact marker. The host can disable it.
- [ ] After surviving 30 seconds, the hider HUD shows 3 live round points. With all free placements
      used, D-pad up buys one extra decoy, replaces the oldest owned one, and reduces both round and
      total score by 3. Paid decoys cannot be placed during Hide.
- [ ] Hitting every prop ends the round: "HUNTERS WIN!", the scoreboard, then a new round.

## 6. Hide & Seek mode

Rules → Mode → Hide & Seek.

- [ ] Hiders stay Link. As the hunter, touching a hider finds them.
- [ ] Hider name tags remain visible wherever their Link models are visible, including at long range.

## 7. Two real players

With a friend (or two copies of Dusklight with different `--mods` folders and profiles):

- [ ] Both see each other animated in the right colours, including sword swings.
- [ ] Sword hits between real players register.
- [ ] The host leaving mid-round: the other player becomes host and the game carries on.

## 8. Game mode

- [ ] Title screen shows a **Hide & Seek** game mode with its own save files.
- [ ] A new file skips the story: you land in Ordon Village in the Hero's Clothes with a sword,
      shield and five hearts, and the Hide & Seek window opens.
- [ ] Faron, Kakariko and Lake Hylia have no twilight.
- [ ] Ordon Village, Kakariko, Lake Hylia, Sacred Grove and Hidden Village load directly into
      normal play: no story camera, forced dialogue, tutorial, minigame, Postman or item popup.
- [ ] The world stays at noon and Link cannot turn into a wolf.
- [ ] NPCs, chests and story triggers cannot start dialogue, item-get scenes or missions.
- [ ] No enemies or bosses remain, including ones spawned by another mod; no encounter-complete
      cutscene or timer starts when they disappear.
- [ ] Damage does not remove hearts and underwater air stays full. A hunter's missed sword swing
      still removes the configured penalty and eliminates at zero when that rule is enabled.
- [ ] Walk/roll/jump toward loading zones in Hide and Hunt, as hider and hunter: movement stops
      before loading. No fade, respawn, lost disguise or change to the countdown occurs.
- [ ] Cross camera-tag areas and room boundaries: the third-person camera remains freely movable.
- [ ] Joining from a normal story save temporarily gives five hearts. Leaving the room restores
      that save's original heart capacity and life.

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

Use Dusklight 2.0.3 and a local WebSocket relay, or the default secure relay. Copy the test bundle into an isolated user directory's `mods`
folder, configure its disc path, and copy a playable save into its `USA/Card A` folder. Launch the
original, unmodified Dusklight with `--user-dir <isolated-dir> --mods <isolated-dir>/mods
--load-save 1 --stage F_SP103,0,13,-1`. Enable frame interpolation and keep the window focused
(or disable pause-on-focus-loss in the isolated profile).

The driver populates 16 player states, starts a round, cycles all 57 selectable props with 160
decoy snapshots and 24 treasure actors, then leaves and checks cleanup. It checks the actual material/shape lists for
cycles before rendering and exits successfully only after logging `STOCK_RENDER_TEST PASS`.
Injected states exercise the real actors and rendering, not 16 independent network clients.

- [ ] Run on stock Linux and Windows, both with interpolation on and off.
- [ ] Separately join real cross-platform clients and place decoys with D-pad up.
- [ ] Repeat rounds, leave/rejoin, change stage, and verify complete animated Link bodies.

Run the same integration bundle with `HS_ARENA_TEST=1` for a fifteen-map sweep. It checks real
idle/walk/sword animation data, camera-tag suppression, blocked native scene changes and compulsory
events, five-heart capacity, and movement collision at authored exit volumes when those actors
exist in the loaded rooms. Follow it with the manual walking/rolling/swimming checks above: an
automated sweep does not explore every pathway or every camera input.
