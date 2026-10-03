# Hyrule Hide & Seek

Online **Prop Hunt** and **Hide & Seek** for Twilight Princess on [Dusklight](https://github.com/TwilitRealm/dusklight).
2 to 16 players. Everyone is Link in their own tunic colour. Props hide as objects, furniture, animals and townspeople;
hunters get a sword and a timer.

## Play

1. Put `hyrule_hide_and_seek.dusk` in Dusklight's `mods` folder (or install it from the in-game mod browser).
2. On the title screen pick the **Hide & Seek** game mode and start a new file. It's a separate save,
   set up as a completed-story sandbox so everyone's world matches: no story cutscenes or quest
   interruptions, enemies or bosses; no twilight; fixed daylight; Hero's Clothes, sword and shield;
   five hearts; and unlimited air underwater.
   (You can also play from any save; the world just might look different for each player.)
3. The Hide & Seek window opens. In **Play**, choose **Host a game** to get a five-letter room code,
   already copied to your clipboard. Friends choose **Join a game** to enter it or pick it from
   **Public games**.
4. Pick the rules in the **Rules** tab, then **Start round**.

The default relay is hosted on Cloudflare. No port forwarding or IP addresses are needed;
players need a network that allows outgoing HTTPS/WebSockets.

Use **Dusklight 2.0.3 or newer**. Its official Linux AppImage fixes the missing WebSocket backend
and HTTPS certificate issue ([release notes](https://github.com/TwilitRealm/dusklight/releases/tag/v2.0.3)).
Linux, Windows and macOS now use the same WebSocket transport. The default public relay requires it;
old Linux builds can still use a private Node relay's HTTPS fallback.
Everyone in a room needs **protocol 10 (mod v0.3.5 or newer)**.
Use **v0.3.6** for live, precise direction arrows. Hunters still on v0.3.5 see their older clue display.
Use **v0.3.7** to fix invisible players on systems with limited game root-heap space.

### A round

| Phase | Props | Hunters |
| --- | --- | --- |
| Get ready | Everyone is warped to the map. | |
| Hide (30 s) | Run, pick a disguise, place decoys, find a spot. | Black screen and a countdown. |
| Hunt (180 s) | Hide, collect treasure, relocate, or buy extra decoys. | Hit every real prop before time runs out. |
| Results | Scoreboard. The next round starts with new hunters. | |

### Controls

| | |
| --- | --- |
| **D-pad right** | Props: copy the pot, crate or barrel next to you (or the next prop) |
| **D-pad left** | Props: previous prop |
| **D-pad up** | Props: place a decoy during Hide or Hunt |
| **D-pad down** | Hiders: taunt (3 s reveal, 4 s cooldown; +1 point every 10 s at most). Hunters: tracking pulse. |
| **B** | Hunters: swing; while swimming, tag a nearby prop. A missed swing costs half a heart by default; zero hearts eliminates you. |

There are 57 selectable props drawn from across Hyrule: pots, crates, furniture, village signs and
targets, forest plants, mountain rocks, Zora water props, plus a walking cucco, Ordon Ranch goats, Hidden Village/Ordon/Castle Town cats,
and Castle Town citizens and shoppers. Milk jars, market baskets and shopping bags expand the
settlement scenery. Native friendly animals and people are preserved. Selection is themed
to the current map. Every disguise is the size of the real object and exactly as solid: you can't
walk through a crate or a fence disguise, but grass stays walk-through.

Each hider starts with three free decoy placements (host-adjustable from 0–10). After those are used,
an extra decoy costs 3 points earned in that round and is available only during Hunt. Every player
can keep ten active, so a full 16-player room can retain 160 decoys without evicting another player's
setup. An eleventh placement replaces only that player's oldest decoy. Decoys are as solid as the
real object, so hiders can block a passage with them; a hunter clears one by hitting it. They give
no hunter points and deliberately count as a missed swing.

### Rules the host can change

Mode (Prop Hunt / Hide & Seek), map (or a random one every round), hiding and round time, number of
hunters, whether found props join the hunters, the miss penalty, how long a prop may stay still
before automatically taunting (Off by default), free decoys per hider, the final clue timer,
hunter tracking pulses, starting rounds automatically, and whether the room is listed
publicly, and collectible treasure. Rules are locked during a round.

The default rules give hiders a choice: stay hidden, or risk a clue to earn points from rupees
and manual taunts. There are no recurring automatic clues. Each surviving hider gets **one exact
location marker for three seconds, with 20 seconds remaining**. The host can set this from
0–60 seconds; 0 means voluntary clues only. Short custom hunts keep their first half quiet.
The final marker fires on time, even after a recent ordinary clue. It marks the reveal location;
moving afterward leaves that spot behind. Stationary clues are optional and default to Off.
These rules work in both game modes, including for the last hider.

Hunters can press D-pad down for a three-second direction and rough range to the nearest hider,
with a 25-second cooldown. It gives no name or exact marker. Auto hunter counts round up to one
per four players on compact maps and one per three on large maps, leaving at least one hider.
Random uses compact maps with fewer than six players; any large map can still be selected.
Everyone starts each round with five hearts. Misses and hits on decoys cost **half a heart** by
default. The host can choose Off, a quarter, half, three quarters, or one heart. At zero hearts,
a hunter watches until the next round. Hiders win immediately when every hunter is out.
Only a confirmed find restores one heart, up to five: spring water, found hearts, potions and
other world healing cannot refill a hunter. Hunters can sheathe their sword normally to use
Horse Grass, call Epona, and interact with the map.
The Rules tab's **Use recommended rules** button restores this balance while keeping your mode,
chosen map and public-room preference. Existing stock rules upgrade automatically; custom rules
are retained. Old stock decoy allowances migrate from five to three.

**Hide & Seek mode**: everyone stays Link and hunters tag hiders by touching them, like SMO Online.

### Treasure and clues

During Hunt, up to **24 spinning rupees** spread across connected, reachable ground, with at least
1,200 units between pickups. The host explores routes during hiding time, so empty areas can get
treasure too. Narrow maps can have fewer pickups. Rupees last 90 seconds, and replacements favor
areas far from other rupees. Collected or expired locations keep a 1,200-unit exclusion for
30 seconds. New pickups stay at least 600 units from active hiders, so standing on a pickup
cannot farm its replacements.

Hiders earn **1 point per pickup**, with up to **2 extra points** for their third pickup. Every
third round is **Treasure Rush**, with **2 points per rupee**. Treasure and manual taunts share a
**6-point bonus limit per round**. Spending on decoys does not reset that limit; further pickups
still reveal a clue but give no points once it is reached.

Each pickup gives hunters a three-second clue: **Ahead / Behind / Left / Right** and
**Near / In the area / Distant**, with an arrow pointing along the exact bearing to the hider.
The arrow pulses around the hunter and rotates continuously with the camera, hunter movement
and hider movement. It points toward the prop without drawing a marker on it.
Manual taunts, optional stationary clues and tracking use the same feedback, without exact
distances or names. The final reveal alone adds a precise world marker.
The text updates with the current direction and rough range for the clue's duration. Hiders can
relocate after taking a risk. Nearby hunters can still hear the positional taunt.

Hiders see a pulsing **CLUE SENT** alert, or **LOCATION REVEALED** for the exact final marker,
with the reason and remaining time. Picking up another rupee cannot hide or extend that reveal.
An upcoming-clue
countdown warns you before the final or optional stationary clue. The host confirms clues for
everyone; spinning in place or taking tiny steps does not reset an enabled stationary timer.

### Clean HUD and mobile menus

The original Dusklight colours and fonts are retained. Compact status cards keep your role, score and timer readable while leaving the sides free for
Dusklight's touch controls. Names stay visible at any distance where the player model is visible;
labels are kept from overlapping. Hunters still cannot see hider names in Prop Hunt.
Only two nearby rupees get a short point label. Taunts use one temporary alert
with a reveal countdown, rather than several lines over the scene.

The menu uses large touch targets and stacks on narrow screens. Results fit all 16 players without
running off-screen. **Control hints** start on, including after upgrading; the Settings toggle
can turn them off, and that choice is saved.
Use the **Guide** tab for the controls, including the touch D-pad on mobile.

### Maps

Ordon Village, Ordon Ranch, Ordon Spring, South Faron Woods, Kakariko Village, Kakariko Graveyard,
Death Mountain Trail, Zora's Domain, Upper Zora's River, Lake Hylia, Castle Town, Sacred Grove,
Hidden Village, Gerudo Desert, Hyrule Field. The selected stage is the play area: scripted doors are
inert, and loading-zone floors and exit volumes block movement before a transition can start.
The arena stays loaded rather than returning you to its spawn. Every arena uses the normal free
third-person field camera, without authored fixed camera zones or story cutscenes.

### Scoring

Both starting roles have the same **24-point round ceiling**:

| Points | Hiders | Starting hunters |
| --- | --- | --- |
| Objective: up to 12 | Awarded live in proportion to the hunt survived (1 per 15 s in a default round). | Every confirmed find advances shared points: 12 × found / starting hiders, rounded down. |
| Bonus: up to 6 | Treasure and manual taunts; taunts give 1 at most every 10 s. | 3 per personal find, or 6 when the round started with only one hider. |
| Win: 6 | Survive until the hiders win. | Find every hider before time runs out. |

Automatic clues give no points. Found hiders retain their survival points and can earn personal
find bonuses from their remaining allowance after becoming hunters; they receive no hunter
progress or win award. Late joiners can help hunt and earn personal bonuses, then participate
fully next round. Longer custom rounds keep the same score ceiling. Paid decoys subtract 3
current-round points and 3 from the session total. Totals otherwise add up for the session.

These limits prevent loot farming or room size alone from inflating one role's score. Map routes,
player skill and the chosen rules still affect which side wins; equal score ceilings do not prove
equal win rates. The [balance notes](docs/balance.md) explain the references and playtest checks.

## Not compatible with

Other multiplayer mods (Crests of Courage, MFB Multiplayer), Randomizer, and mods that change story
flags or stage layouts: turn them off while playing this.

## For developers

```
src/        the mod (C++, Dusklight mod SDK)
server/     the relay: Cloudflare Worker + a plain Node version, tests and test bots
tests/      rules test (no game needed) and a tunic-colour preview
tools/      disc tools for safe spawns, native actor layouts and exact-case prop validation
docs/       design notes and the in-game test checklist
```

### Build

```sh
cmake -B build -G Ninja      # fetches Dusklight v2.0.3 into ./dusklight
cmake --build build          # build/mods/hyrule_hide_and_seek.dusk (this platform only)
```

A local build only works on your own platform. Push to GitHub and the workflow in
`.github/workflows/build.yml` builds all eight platforms and merges them into one `.dusk`.

### Relay hosting

The default relay is `wss://hyrule-hide-and-seek.jhackerr.workers.dev`.
Rooms are isolated Durable Objects with at most 16 players each, so separate lobbies can run
concurrently. The free plan has daily quotas: continuous play by hundreds of players cannot stay
free indefinitely. See [capacity and quota estimates](server/README.md#free-tier-capacity). To run another relay:

- On Cloudflare (nothing runs at home): [server/README.md](server/README.md).
- On your own machine, e.g. a Raspberry Pi, through Tailscale Funnel (no open ports, home IP hidden):
  [server/deploy/README.md](server/deploy/README.md).

### Tests

```sh
tests/run.sh                    # rules, balance, saved settings, interpolation and layout (4,500+ checks)
cd server && npm install && npm test   # the relay and the bots
```

`tests/recolor_preview.py <disc> out.png` renders every tunic colour from your own game files.
`tools/validate_prop_assets.py <disc>` checks every archive, model and animation name against an
extracted disc, including exact letter case and numeric model IDs.

### Try it alone

```sh
cd server
node node-server.mjs                     # a relay on ws://127.0.0.1:8787
node bots.mjs --room ABCDE --count 3    # after hosting in game
```

In the game, Settings → **Use a server on this computer**, host a room, then start the bots with its
code. They follow you onto the map, turn into props you can hunt, or hunt you.

The in-game checks still to do are in [docs/testing.md](docs/testing.md).

## Credits

- **i12bp8**: the mod.
- **[TwilitRealm](https://github.com/TwilitRealm)**: Dusklight and its mod SDK.
- **[zeldaret/tp](https://github.com/zeldaret/tp)**: the decompilation Dusklight is built on.
- **remiafterdark** ([Crests of Courage](https://github.com/remiafterdark/crests-of-courage), MIT) and
  **thevipguy** ([MFB Multiplayer](https://github.com/thevipguy/dusklight-online-mod), CC0): their
  open-source co-op mods showed how to draw other players, run a room server on Cloudflare and draw
  a HUD. No code was copied; the approaches are credited here.
- SMO Online's hide & seek and Garry's Mod's Prop Hunt, for the rules.

Twilight Princess belongs to Nintendo. This mod contains no game files: every model, texture,
animation and sound is read from your own copy of the game at runtime.

### AI assistance

The code, relay, tests and documentation were written with help from AI assistants (Claude by
Anthropic and Codex by OpenAI) and reviewed by i12bp8. Tag it as AI-assisted when uploading.
