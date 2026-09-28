# Checking it in the game

What's been verified without the game:

- Everything compiles for Linux with GCC and Clang against Dusklight v2.0.2 (the other platforms build in CI).
- The rules (`tests/run.sh`): rounds, hunter rotation, gather timeout, hit validation, tag immunity,
  scoring, late joiners, host leaving mid-round, unique colours.
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

- [ ] Log shows `Hyrule Hide & Seek 0.1.0 ready`. No "hook ... did not resolve" warnings for
      `daAlink_c::execute`, `daAlink_c::draw`, `daAlink_c::setCutType`.
- [ ] Hide & Seek tab opens the window with Play / Rules / How to play / Settings.
- [ ] **Host a game** → toast with a code, code is on the clipboard, HUD shows `Room ABCDE` at the top.

## 2. Other players are drawn

`node bots.mjs --room ABCDE --count 3`

- [ ] Three Links appear around you, each a different tunic colour, standing (idle animation).
- [ ] Walk away: they run after you with the running animation, feet on the ground, shadows under them.
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
- [ ] The cucco prop animates when you walk.
- [ ] The hunter bot waits at the spawn during Hide, then chases you during the hunt and tags you:
      "You were found!", you become Link again (and a hunter).
- [ ] D-pad down taunts: a Link shout plays, the taunt bar under your role fills back up.

## 5. A round as the hunter

Restart until you're the hunter (1 in 4).

- [ ] Hide phase: black screen with a countdown, you can't walk away from the spawn.
- [ ] The bots are props standing near you (pots, crates...). No name tags over them.
- [ ] Sword swing (B) hitting a bot prop: spark, "Found Bot N!", the counter at the top drops.
- [ ] Swinging at nothing costs a quarter heart (never the last one). Hitting a real pot also costs one.
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
