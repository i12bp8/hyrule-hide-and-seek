# Local filming session

These tools made the October 1, 2026 Discord share pack from real Dusklight gameplay with six
WebSocket bots. Camera placement, bot routes, and initial role selection are staged. Disguises,
decoys, clues, sword hits, tag checks, infection, animation, and scoring use the mod's normal code.
Keep the staged-bot disclosure when posting this footage.

The director and bots now use protocol 11. The `chase` take is a Prop Hunt chase; old
`hide-and-seek` filenames remain for the existing export script and do not select a removed mode.

The director is enabled only by `HS_SHOWCASE=ON`, with a further `HS_SHOWCASE_CONTROL` environment
variable gate. The default build and release workflow exclude it. Never distribute the filming
`.dusk` as a release or use it in someone else's online room.

## Reproduce on this machine

Requires the installed Dusklight 2.0.3 AppImage, your own disc and Hide & Seek save, Node, FFmpeg,
Grim, GPU Screen Recorder, and the existing `server/node_modules`. The capture helper uses
`~/Downloads/Dusklight.AppImage`, the display `eDP-1`, and the `USA` save naming convention.
Adapt those paths/display names for another machine.

```sh
cmake -S . -B build-showcase -G Ninja -DHS_SHOWCASE=ON
cmake --build build-showcase --parallel 6
python3 tools/showcase/capture.py prepare

# Keep this relay running in a separate terminal:
cd server
node node-server.mjs
```

In a second terminal, from the repository root:

```sh
python3 tools/showcase/capture.py start
```

`prepare` reads your normal Dusklight configuration and save, then writes an isolated profile
under `work/showcase/profile`. It copies the completed Hide & Seek save and changes only that
copy's CARD filename for `--load-save`. Autosave is disabled in the filming profile. `start`
launches the game, waits for its private local room, and joins six bots automatically.

Put the game fullscreen and remove desktop popups before recording. This director injects input
and controls the camera for filming; use the standard build and `server/bots.mjs` for normal play.
Gameplay keeps running when the game loses focus. Captures record the full display plus desktop
audio, so leave the game visible during each take.

```sh
python3 tools/showcase/capture.py command camera 0 870 310 48 0 -150
python3 tools/showcase/capture.py snapshot 01-colour-crew.png
python3 tools/showcase/capture.py take props
python3 tools/showcase/capture.py take chase
python3 tools/showcase/capture.py take cats
python3 tools/showcase/capture.py take hunt
python3 tools/showcase/capture.py stop
python3 tools/showcase/export.py
```

Raw takes and logs stay in `work/showcase`; the shareable files are in
`work/discord-share-2026-10-01`. Game data, profiles, and media are ignored by Git. The capture
helper can also accept `command`, `bots` JSON, `snapshot`, `record NAME SECONDS`, and `status`.
Each recording can be overwritten by repeating the take; copy a take before replacing it.

The exports are 1080p, 30 fps H.264/AAC MP4 with fast-start metadata and a staged-session label.
The exporter checks resolution, codecs, pixel format, audio, duration metadata, and a 9.5 MB
maximum per video. Audio is the captured game audio, boosted by 16 dB; review it when using a
different system volume. The cover and gallery are presentation artwork using these actual
screenshots; they do not add fake gameplay objects.
