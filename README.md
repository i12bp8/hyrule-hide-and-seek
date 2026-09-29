# Hyrule Hide & Seek

Online **Prop Hunt** and **Hide & Seek** for Twilight Princess on [Dusklight](https://github.com/TwilitRealm/dusklight).
2 to 16 players. Everyone is Link in their own tunic colour. Props hide as pots, crates and barrels;
hunters get a sword and a timer.

## Play

1. Put `hyrule_hide_and_seek.dusk` in Dusklight's `mods` folder (or install it from the in-game mod browser).
2. On the title screen pick the **Hide & Seek** game mode and start a new file. It's a separate save,
   set up as a completed-story sandbox so everyone's world matches: no story cutscenes or quest
   interruptions, enemies or bosses; no twilight; fixed daylight; Hero's Clothes, sword and shield;
   six hearts; and unlimited air underwater.
   (You can also play from any save; the world just might look different for each player.)
3. The Hide & Seek window opens. **Host a game**: you get a five-letter room code, already copied to
   your clipboard. Friends type it in and press **Join**, or pick your room from **Public games**.
4. Pick the rules in the **Rules** tab, then **Start round**.

Nothing to set up: no port forwarding, no IP addresses. It works on home wifi, mobile data and
school networks.

On the official Linux build of Dusklight 2.0.2, the mod automatically uses its HTTP fallback for
the upstream [missing WebSocket backend](https://github.com/TwilitRealm/dusklight/issues/2629).
Windows, macOS and fixed Linux builds use WebSockets.

### A round

| Phase | Props | Hunters |
| --- | --- | --- |
| Get ready | Everyone is warped to the map. | |
| Hide (45 s) | Run, pick a disguise, find a spot. | Black screen and a countdown. |
| Hunt (4 min) | Stay still. Taunt for points. | Hit every prop with your sword before time runs out. |
| Results | Scoreboard. The next round starts with new hunters. | |

### Controls

| | |
| --- | --- |
| **D-pad right** | Props: copy the pot, crate or barrel next to you (or the next prop) |
| **D-pad left** | Props: previous prop |
| **D-pad down** | Props: taunt (+1 point, every 5 s) |
| **B** | Hunters: swing. Hitting nothing costs a quarter heart. |

Props: pot, big pot, crate, small crate, barrel, skull, pumpkin, Kakariko pot, rock and a walking cucco.

### Rules the host can change

Mode (Prop Hunt / Hide & Seek), map (or a random one every round), hiding and round time, number of
hunters, whether found props join the hunters, the miss penalty, last-minute auto-taunts, starting
rounds automatically, and whether the room is listed publicly.

**Hide & Seek mode**: everyone stays Link and hunters tag hiders by touching them, like SMO Online.

### Maps

Ordon Village, Ordon Ranch, Ordon Spring, South Faron Woods, Kakariko Village, Kakariko Graveyard,
Death Mountain Trail, Zora's Domain, Upper Zora's River, Lake Hylia, Castle Town, Sacred Grove,
Hidden Village, Gerudo Desert, Hyrule Field. The selected stage is the play area: scripted doors are
inert, and a loading zone that leaves it returns you to its spawn so nobody can escape the round.

### Scoring

Props get 1 point per 10 seconds hidden, 5 for surviving the round, and 1 per taunt. Hunters get 5
per find. Scores add up over the whole session.

## Not compatible with

Other multiplayer mods (Crests of Courage, MFB Multiplayer), Randomizer, and mods that change story
flags or stage layouts: turn them off while playing this.

## For developers

```
src/        the mod (C++, Dusklight mod SDK)
server/     the relay: Cloudflare Worker + a plain Node version, tests and test bots
tests/      rules test (no game needed) and a tunic-colour preview
tools/      find_spawns.py: lists safe spawn points from your own disc files
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
tests/run.sh                    # the rules: rounds, hits, scoring, host migration (~280 checks)
cd server && npm install && npm test   # the relay and the bots
```

`tests/recolor_preview.py <disc> out.png` renders every tunic colour from your own game files.

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

The code, relay, tests and documentation were written with the help of an AI assistant (Claude, by
Anthropic) and reviewed by i12bp8. Tag it as AI-assisted when uploading.
