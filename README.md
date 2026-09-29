# Hyrule Hide & Seek

Online **Prop Hunt** and **Hide & Seek** for Twilight Princess on [Dusklight](https://github.com/TwilitRealm/dusklight).
2 to 16 players. Everyone is Link in their own tunic colour. Props hide as objects and furniture;
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

Nothing to set up: no port forwarding, no IP addresses. It works on home wifi, mobile data and
school networks.

On the official Linux build of Dusklight 2.0.2, the mod automatically uses its HTTP fallback for
the upstream [missing WebSocket backend](https://github.com/TwilitRealm/dusklight/issues/2629).
Windows, macOS and fixed Linux builds use WebSockets.
The crash fixes are part of the mod; an unmodified Dusklight 2.0.2 is sufficient. On builds using
the HTTP fallback, custom relays must use HTTPS (plain localhost HTTP is not supported).

### A round

| Phase | Props | Hunters |
| --- | --- | --- |
| Get ready | Everyone is warped to the map. | |
| Hide (30 s) | Run, pick a disguise, place decoys, find a spot. | Black screen and a countdown. |
| Hunt (180 s) | Stay hidden; taunt or buy extra decoys with round points. | Hit every real prop before time runs out. |
| Results | Scoreboard. The next round starts with new hunters. | |

### Controls

| | |
| --- | --- |
| **D-pad right** | Props: copy the pot, crate or barrel next to you (or the next prop) |
| **D-pad left** | Props: previous prop |
| **D-pad up** | Props: place a decoy during Hide or Hunt |
| **D-pad down** | Hiders: taunt (3 s reveal, 4 s cooldown; +1 point every 10 s at most). Hunters: tracking pulse. |
| **B** | Hunters: swing; while swimming, tag a nearby prop. A miss costs a quarter heart. |

There are 50 selectable props drawn from across Hyrule: pots, crates, furniture, village signs and
targets, forest plants, mountain rocks, Zora water props, plus a walking cucco. Selection is themed
to the current map. Every disguise is the size of the real object and exactly as solid: you can't
walk through a crate or a fence disguise, but grass stays walk-through.

Each hider starts with five free decoy placements (host-adjustable from 0–10). After those are used,
an extra decoy costs 3 points earned in that round and is available only during Hunt. Every player
can keep ten active, so a full 16-player room can retain 160 decoys without evicting another player's
setup. An eleventh placement replaces only that player's oldest decoy. Decoys are as solid as the
real object, so hiders can block a passage with them; a hunter clears one by hitting it. They give
no hunter points and deliberately count as a missed swing.

### Rules the host can change

Mode (Prop Hunt / Hide & Seek), map (or a random one every round), hiding and round time, number of
hunters, whether found props join the hunters, the miss penalty, how long a prop may stay still
before automatically taunting (20 seconds by default, or Off), free decoys per hider, regular
taunt clues, hunter tracking pulses, starting rounds automatically, and whether the room is listed
publicly.

The default rules keep the hunt moving: automatic clues every 30 seconds (20 on large maps),
then every 10 seconds in the last minute. Each clue lasts three seconds. Manual and stationary
taunts satisfy the same timer, so clues don't stack. These work in both game modes.

Hunters can press D-pad down for a three-second direction and rough range to the nearest hider,
with a 25-second cooldown. It gives no name or exact marker. Auto hunter counts round up to one
per four players on compact maps and one per three on large maps, leaving at least one hider.
Random uses compact maps with fewer than six players; any large map can still be selected.
Everyone starts each round with five hearts, and a miss never takes the final quarter.
The Rules tab's **Use recommended rules** button restores this balance while keeping your mode,
chosen map and public-room preference. Existing stock rules upgrade automatically; custom rules
are retained. Everyone in a room needs v0.2.0 or newer (protocol 6).

**Hide & Seek mode**: everyone stays Link and hunters tag hiders by touching them, like SMO Online.

### Maps

Ordon Village, Ordon Ranch, Ordon Spring, South Faron Woods, Kakariko Village, Kakariko Graveyard,
Death Mountain Trail, Zora's Domain, Upper Zora's River, Lake Hylia, Castle Town, Sacred Grove,
Hidden Village, Gerudo Desert, Hyrule Field. The selected stage is the play area: scripted doors are
inert, and loading-zone floors and exit volumes block movement before a transition can start.
The arena stays loaded rather than returning you to its spawn. Every arena uses the normal free
third-person field camera, without authored fixed camera zones or story cutscenes.

### Scoring

Props earn 1 point live for every 10 seconds hidden, 5 for surviving the round, and 1 per taunt at
most once every 10 seconds. Hunters get 5 per find. Paid decoys subtract 3 current-round points and
3 from the room total, so they are a real tactical tradeoff. Scores otherwise add up for the session.

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
cmake -B build -G Ninja      # fetches Dusklight v2.0.2 into ./dusklight
cmake --build build          # build/mods/hyrule_hide_and_seek.dusk (this platform only)
```

A local build only works on your own platform. Push to GitHub and the workflow in
`.github/workflows/build.yml` builds all eight platforms and merges them into one `.dusk`.

### Relay hosting

The default relay runs on a Raspberry Pi at
`wss://hyrule-hide-and-seek.tail5c3d0e.ts.net` through Tailscale Funnel. To run another relay:

- On Cloudflare (nothing runs at home): [server/README.md](server/README.md).
- On your own machine, e.g. a Raspberry Pi, through Tailscale Funnel (no open ports, home IP hidden):
  [server/deploy/README.md](server/deploy/README.md).

### Tests

```sh
tests/run.sh                    # rules, balance, saved settings and exit collision (2,500+ checks)
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
