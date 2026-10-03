#!/usr/bin/env python3
"""Drive the opt-in in-game test harness (HS_LAB) for screenshots of props and arenas.

    cmake -S . -B build-lab -G Ninja -DHS_LAB=ON && cmake --build build-lab
    python3 tools/lab/lab.py prepare          # isolated profile under work/lab (copies your save)
    python3 tools/lab/lab.py start --bots 2   # relay + game on a headless Hyprland output + bots
    python3 tools/lab/lab.py cmd round 3 hider 600 600
    python3 tools/lab/lab.py shot kakariko-spawn
    python3 tools/lab/lab.py stop

The game window goes to a headless output (HSLAB) so the desktop is not disturbed. Screenshots go
to work/lab/shots. Nothing here is part of a release build.
"""
import argparse
import json
import os
import signal
import shutil
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "work" / "lab"
PROFILE = WORK / "profile"
CONTROL = WORK / "control.txt"
SHOTS = WORK / "shots"
APPIMAGE = Path.home() / "Downloads" / "Dusklight.AppImage"
OUTPUT = "HSLAB"


def command(*words):
    serial = time.time_ns() // 1000000
    tmp = CONTROL.with_suffix(".tmp")
    tmp.write_text(f"{serial} {' '.join(str(w) for w in words)}\n")
    tmp.replace(CONTROL)
    time.sleep(0.35)


def status():
    try:
        v = Path(str(CONTROL) + ".status").read_text().split()
        keys = ["room", "phase", "round", "role", "x", "y", "z", "yaw", "stage", "map", "edge",
                "members", "prop", "disguised"]
        out = dict(zip(keys, v))
        for k in ("phase", "round", "role", "map", "members", "prop", "disguised", "yaw"):
            if k in out:
                out[k] = int(out[k])
        for k in ("x", "y", "z", "edge"):
            if k in out:
                out[k] = float(out[k])
        return out
    except (FileNotFoundError, ValueError):
        return {}


def wait_for(predicate, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        s = status()
        if s and predicate(s):
            return s
        time.sleep(0.25)
    raise RuntimeError(f"timed out: {status()}")


def shot(name, scale=50):
    SHOTS.mkdir(parents=True, exist_ok=True)
    full = SHOTS / f"{name}.png"
    subprocess.run(["grim", "-o", OUTPUT, str(full)], check=True)
    if scale != 100:
        subprocess.run(["magick", str(full), "-resize", f"{scale}%", str(full)], check=True)
    print(full, flush=True)
    return full


def prepare():
    source = Path.home() / ".local/share/TwilitRealm/Dusklight"
    (PROFILE / "mods").mkdir(parents=True, exist_ok=True)
    card = PROFILE / "USA/Card A"
    card.mkdir(parents=True, exist_ok=True)
    original = json.loads((source / "config.json").read_text())
    config = {k: v for k, v in original.items()
              if not k.startswith("mod.") or k.startswith("mod.com_i12bp8_hyrule__hide__and__seek.")}
    config.update({"game.pauseOnFocusLost": False, "game.autoSave": False,
                   "game.enableAchievementToasts": False, "game.enableControllerToasts": False,
                   "mod.com_i12bp8_hyrule__hide__and__seek.server": "ws://127.0.0.1:8787",
                   "mod.com_i12bp8_hyrule__hide__and__seek.tunic_color": 0})
    (PROFILE / "config.json").write_text(json.dumps(config, indent=2) + "\n")
    save = bytearray((source / "USA/Card A/01-GZ2E-hyrule-hide-and-seek.gci").read_bytes())
    if bytes(save[8:40]).rstrip(b"\0") != b"hyrule-hide-and-seek":
        raise RuntimeError("unexpected save identity; refusing to rewrite the copied header")
    save[8:40] = b"gczelda2".ljust(32, b"\0")
    (card / "01-GZ2E-gczelda2.gci").write_bytes(save)
    install()
    print(PROFILE)


def install(bundle=None):
    (PROFILE / "mods").mkdir(parents=True, exist_ok=True)
    for old in (PROFILE / "mods").glob("*.dusk"):
        old.unlink()
    shutil.copy2(bundle or ROOT / "build-lab/mods/hyrule_hide_and_seek.dusk", PROFILE / "mods")


def ensure_output():
    monitors = subprocess.run(["hyprctl", "monitors"], capture_output=True, text=True).stdout
    if f"Monitor {OUTPUT}" not in monitors:
        subprocess.run(["hyprctl", "output", "create", "headless", OUTPUT], check=True)
        time.sleep(1)
    out = subprocess.run(["hyprctl", "monitors", "-j"], capture_output=True, text=True).stdout
    for m in json.loads(out):
        if m["name"] == OUTPUT:
            return m["activeWorkspace"]["id"]
    raise RuntimeError("no headless output")


def start(bots, stage):
    WORK.mkdir(parents=True, exist_ok=True)
    CONTROL.write_text("0 wait\n")
    Path(str(CONTROL) + ".status").write_text("")
    processes = {}
    relay_log = open(WORK / "relay.log", "w")
    relay = subprocess.Popen(["node", str(ROOT / "server/node-server.mjs")], stdout=relay_log,
                             stderr=relay_log, start_new_session=True)
    processes["relay"] = relay.pid
    time.sleep(1)
    workspace = ensure_output()
    script = WORK / "run.sh"
    script.write_text(
        "#!/bin/sh\n"
        f"export HS_LAB_CONTROL='{CONTROL}'\n"
        "export BOREALIS_SENTRY_ENABLED=0\n"
        f"exec stdbuf -o0 -e0 '{APPIMAGE}' --user-dir '{PROFILE}' --mods '{PROFILE}/mods' --load-save 1 "
        f"--stage '{stage}' --log-level info > '{WORK}/game.log' 2>&1\n")
    script.chmod(0o755)
    subprocess.run(["hyprctl", "eval", f"hl.exec_cmd('{script}', {{workspace = '{workspace} silent'}})"],
                   check=True, capture_output=True)
    s = wait_for(lambda s: s.get("room"), timeout=90)
    time.sleep(1)
    for line in subprocess.run(["pgrep", "-f", str(PROFILE)], capture_output=True, text=True).stdout.split():
        processes.setdefault("game", int(line))
    if bots:
        bot_log = open(WORK / "bots.log", "w")
        b = subprocess.Popen(["node", str(ROOT / "server/bots.mjs"), "--room", s["room"], "--count",
                              str(bots)], stdout=bot_log, stderr=bot_log, start_new_session=True)
        processes["bots"] = b.pid
        wait_for(lambda s: s.get("members", 0) >= bots + 1, timeout=30)
    (WORK / "processes.json").write_text(json.dumps(processes))
    print(f"lab running: room {s['room']} {processes}", flush=True)


def stop():
    manifest = WORK / "processes.json"
    if manifest.exists():
        for key, pid in json.loads(manifest.read_text()).items():
            try:
                os.kill(pid, signal.SIGINT if key == "game" else signal.SIGTERM)
            except ProcessLookupError:
                pass
        manifest.unlink()
    for line in subprocess.run(["pgrep", "-f", str(PROFILE)], capture_output=True, text=True).stdout.split():
        try:
            os.kill(int(line), signal.SIGINT)
        except ProcessLookupError:
            pass
    print("lab stopped")


def render_test(heap, arenas):
    """Run the full-room rendering check in the same isolated profile and hidden output."""
    stop()
    install(ROOT / "build-stock-v04/mods/hyrule_hide_and_seek.dusk")
    relay_log = open(WORK / "relay.log", "w")
    relay = subprocess.Popen(["node", str(ROOT / "server/node-server.mjs")], stdout=relay_log,
                             stderr=relay_log, start_new_session=True)
    processes = {"relay": relay.pid}
    (WORK / "processes.json").write_text(json.dumps(processes))
    time.sleep(1)
    workspace = ensure_output()
    script = WORK / "run-render.sh"
    log = WORK / "render.log"
    argv = ["stdbuf", "-o0", "-e0", str(APPIMAGE), "--user-dir", str(PROFILE),
            "--mods", str(PROFILE / "mods"), "--load-save", "1",
            "--stage", "F_SP103,0,13,-1", "--log-level", "info"]
    script.write_text("#!/bin/sh\nexport BOREALIS_SENTRY_ENABLED=0\n" +
                      ("export HS_HEAP_TEST=1\n" if heap else "") +
                      ("export HS_ARENA_TEST=1\n" if arenas else "") +
                      "exec " + shlex.join(argv) + " > " + shlex.quote(str(log)) + " 2>&1\n")
    script.chmod(0o755)
    subprocess.run(["hyprctl", "eval", f"hl.exec_cmd('{script}', {{workspace = '{workspace} silent'}})"],
                   check=True, capture_output=True)
    time.sleep(1)
    for pid in subprocess.run(["pgrep", "-f", str(PROFILE)], capture_output=True, text=True).stdout.split():
        processes.setdefault("game", int(pid))
    (WORK / "processes.json").write_text(json.dumps(processes))
    print(f"render test running: {processes}; log: {log}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("prepare")
    sub.add_parser("install")
    p = sub.add_parser("start")
    p.add_argument("--bots", type=int, default=1)
    p.add_argument("--stage", default="F_SP103,0,13,-1")
    sub.add_parser("stop")
    p = sub.add_parser("render")
    p.add_argument("--heap", action="store_true", help="also exercise heap pressure and failed resource loads")
    p.add_argument("--arenas", action="store_true", help="check every arena's camera, events, exits and health")
    sub.add_parser("status")
    p = sub.add_parser("cmd")
    p.add_argument("words", nargs="+")
    p = sub.add_parser("shot")
    p.add_argument("name")
    p.add_argument("--scale", type=int, default=50)
    args = parser.parse_args()
    if args.cmd == "prepare":
        prepare()
    elif args.cmd == "install":
        install()
    elif args.cmd == "start":
        start(args.bots, args.stage)
    elif args.cmd == "stop":
        stop()
    elif args.cmd == "render":
        render_test(args.heap, args.arenas)
    elif args.cmd == "status":
        print(json.dumps(status()))
    elif args.cmd == "cmd":
        command(*args.words)
    elif args.cmd == "shot":
        shot(args.name, args.scale)


if __name__ == "__main__":
    main()
