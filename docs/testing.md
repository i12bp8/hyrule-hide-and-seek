# v0.4.1 verification and game checklist

## Recorded checks

On official Linux Dusklight 2.0.3:

- 5,659 rules/protocol/settings/interpolation/layout/RARC assertions passed.
- All 16 Node relay/bot tests passed, including protocol-12 roster parsing.
- All 76 catalogue entries (75 selectable plus treasure) resolved: 217 resources in 70 exact-case
  archives, including models, alternate animations, secondary models and collision meshes.
- The arena driver passed camera, event, exit and five-heart checks on all 11 whole maps with
  their 417 added scenery objects. Exit-volume crossings were checked where those actors were loaded.
- The stock renderer completed all 75 disguises with 160 decoys, 24 rupees and cleanup under
  32 MiB free root-heap pressure. It checked 211,415 real draw lists and verified transient
  heap/model/animation failures, collision registration recovery and three heap destruction/recreation cycles.
- Every cat, dog and cucco coat was rendered side by side and keeps its coat over time.

These are Linux checks. Injected player states exercise real actors and rendering, not 16
independent clients. Asset existence does not establish visual fidelity or balance. Other-platform
runtime checks, real mobile input and paired human playtests remain necessary.

## Reproduce automated checks

```sh
tests/run.sh
cd server
npm ci --omit=dev
npm test
```

Validate against an extracted disc (the game files are not part of this repository):

```sh
python3 tools/validate_prop_assets.py /path/to/disc/files
```

### Isolated game lab

The Linux helper assumes Hyprland, `grim`, ImageMagick, Dusklight at
`~/Downloads/Dusklight.AppImage` and an existing dedicated save using the USA naming convention.
It copies configuration/save into `work/lab/profile`, disables autosave and uses a hidden `HSLAB`
output. Adjust paths for another machine. Release builds leave both lab and test drivers off.

```sh
cmake -B build-lab -G Ninja -DHS_LAB=ON
cmake --build build-lab --parallel
python3 tools/lab/lab.py prepare
python3 tools/lab/lab.py start --bots 2
python3 tools/lab/lab.py cmd round 9 hider 600 600
python3 tools/lab/lab.py shot telmas-bar
python3 tools/lab/lab.py stop
```

### Stock renderer and arena regression

```sh
cmake -B build-stock-v04 -G Ninja -DHS_STOCK_RENDER_TEST=ON
cmake --build build-stock-v04 --parallel
python3 tools/lab/lab.py render --heap
# Read work/lab/render.log until STOCK_RENDER_TEST PASS (or FAIL).
python3 tools/lab/lab.py render --arenas
python3 tools/lab/lab.py stop
```

The normal stock driver populates 16 player states, cycles every disguise with 160 decoy
snapshots and 24 treasure actors, then leaves. It checks real retained material/shape lists for
cycles, animated idle/walk/sword BCKs, and recovery after a failed collision registration.
`HS_HEAP_TEST=1` additionally constrains root free memory to 32767 KiB, checks host-backed heap
lookup/alignment and three reload cycles, then injects heap, model and animation read failures.
All 15 remote lobby puppets must become visible before the prop stress test starts.

`HS_ARENA_TEST=1` visits each map and checks the free camera, camera-tag suppression, blocked
scene changes, compulsory events, five-heart capacity and crossing authored exit volumes when
those actors exist in loaded rooms. The full manual route checks below are still required.
`HS_DIRECTION_TEST=1` runs the focused native bearing regression for moving hunters/props/camera,
expiry and found/disconnected/stale/off-stage suppression. Never distribute a test bundle.

## Manual game checks

Keep the `com.i12bp8.hyrule_hide_and_seek` log visible. Use a local relay and bots, then repeat with
real clients on separate networks. Everyone must use protocol 11.

### Loading, saves and migration

- [ ] Title-screen **Prop Hunt** creates a separate completed-story sandbox; existing dedicated saves work.
- [ ] Log reports v0.4.1 / protocol 12 and no missing hook warnings.
- [ ] Both roles load every arena; repeat Telma's Bar after changing disguises and maps.
- [ ] No story dialogue, cutscene, enemy, boss, item popup or forced camera starts in a round.
- [ ] Roll, jump and swim toward exits/voids; no scene transition, fade or respawn occurs.
- [ ] Leave an online round from a story save; its original heart capacity/life return.
- [ ] Old stock rules migrate; old map choices reset to Random; custom timings/options persist.
- [ ] Change host after spending, finds and swaps; scores, limits and remaining cooldowns persist.
- [ ] Quit Dusklight normally after repeated rounds without a shutdown crash.

### Props and scenery

- [ ] Every map has a varied themed palette, usable hiding routes and added native clutter.
- [ ] Compare real and disguised pots, crates, barrels, furniture, rocks, grasses, graves, animals
      and citizens: size, lighting, idle/walk motion and shadows match at rest and while moving.
- [ ] Lily pads rest on the water surface, bob and can be hit above swimming props.
- [ ] Grass sways/bends; oil jars draw both their body and animated oil surface.
- [ ] No crystal choice or invisible disguise; when a resource fails, Link remains visible.
- [ ] No A/Z interaction prompt reveals a real pot, pumpkin or grass during Hide/Hunt.
- [ ] Solid props/decoys block Link. A placer can step clear without becoming trapped. Nearby
      furniture collision recovers when the mesh table has temporarily filled.
- [ ] A hunter sees no prop name tags. Friendly native animals/people remain in appropriate arenas.

### Abilities, hunting and scoring

- [ ] Tap Up places a decoy; hold Up for 0.45 s swaps with the newest one during Hunt only.
      Check twice-per-round limit, 15 s cooldown, yaw and the decoy at the old position.
- [ ] Every cat, dog and cucco coat is selectable, keeps its coat while worn and as a decoy, and blinks;
      D-pad right next to a real animal copies its coat.
- [ ] Whole maps: walk to each loading exit (it holds you back), shut doors stay shut, no invisible fence elsewhere.
- [ ] Down taunts as a prop or tracks as a hunter; clue arrows update with movement/camera.
- [ ] Three free placements; later decoys cost 3 round/session points. Ten per player; replacing
      one player's oldest never removes another player's setup.
- [ ] Sword hits and nearby swimming B tags find props; decoy hits remove them and count as misses.
- [ ] Half-heart miss cost by default; test Off and every quarter-heart setting. Zero eliminates;
      only a find heals one heart. All hunters out immediately gives props the win.
- [ ] Both roles cap at 12 objective + 8 gross bonus + 6 win. Test 1v1 and 4/8/16 players.
- [ ] Bold taunts, decoy fooled, close calls, first blood, quick finds and last standing show their
      feedback and stop at their limits/shared cap. Manual early stopping gives no last-standing award.
- [ ] Infection does not grant a second objective/win award; late joiners fully participate next round.
- [ ] No recurring/stationary clues by default; final marker fires once at 20 s left and stays at
      the revealed position. Recent loot/taunts neither postpone nor prolong it.
- [ ] Loot is reachable, separated and away from exits/water/cliffs. Pickups live 90 s; removed
      areas stay excluded for 30 s. Normal 1-point pickups, third-pickup +2 and Rush +2 respect the cap.

### Every arena

| Arena | Load/events | Routes/treasure | Props/shadows | Paired human balance |
| --- | --- | --- | --- | --- |
| Ordon Village | | | | |
| Ordon Ranch | | | | |
| Kakariko Village | | | | |
| Kakariko Graveyard | | | | |
| Death Mountain | | | | |
| Castle Town | | | | |
| Sacred Grove | | | | |
| Hidden Village | | | | |
| Bulblin Camp | | | | |
| Telma's Bar | | | | |
| Hyrule Castle Grounds | | | | |

Record per-map wins, hunt time, finds and gross points with roles exchanged; follow
[balance.md](balance.md). Passing loading/render checks does not establish a 50% win rate.

### Devices, interface and network

- [ ] Linux/Windows/macOS: full animated bodies, sword hits, leaving/rejoining, host migration;
      interpolation on/off and 50–200 ms latency.
- [ ] Android/iOS: touch Host/Join, dropdowns, public list, D-pad taps/holds, portrait/landscape safe areas.
- [ ] Original theme/fonts remain; narrow menus stack; touch controls remain clear of status cards.
- [ ] Names/alerts/treasure labels avoid overlap; all 16 result rows fit; control hints default On
      and the saved Off preference survives restart.
- [ ] Public WSS hosting/joining from separate networks and public listings work. Old protocol rooms reject joins.
