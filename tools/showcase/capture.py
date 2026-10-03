#!/usr/bin/env python3
"""Local capture helper. Only the opt-in filming profile is controlled."""
import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'work/showcase'
CONTROL = WORK / 'control.txt'
BOT_CONTROL = WORK / 'bots.json'
OUTPUT = ROOT / 'work/discord-share-2026-10-01'

def command(action):
    serial = int(time.time_ns() // 1000000 % 2_000_000_000)
    tmp = CONTROL.with_suffix('.tmp')
    tmp.write_text(f'{serial} {action}\n')
    tmp.replace(CONTROL)
    time.sleep(0.3)

def bots(**values):
    previous = json.loads(BOT_CONTROL.read_text()) if BOT_CONTROL.exists() else {}
    previous.update(values)
    tmp = BOT_CONTROL.with_suffix('.tmp')
    tmp.write_text(json.dumps(previous))
    tmp.replace(BOT_CONTROL)

def state():
    try:
        v = Path(str(CONTROL) + '.status').read_text().split()
        return dict(room=v[0], phase=int(v[1]), round=int(v[2]), role=int(v[3]),
                    x=float(v[4]), y=float(v[5]), z=float(v[6]), hiders=int(v[7]))
    except (IndexError, ValueError, FileNotFoundError):
        return {}

def wait_phase(phase, timeout=40):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if state().get('phase') == phase:
            return state()
        time.sleep(0.2)
    raise RuntimeError(f'Phase {phase} timed out: {state()}')

def anchor_bots():
    s = state()
    bots(take=time.time_ns(), x=s['x'], y=s['y'], z=s['z'])

def select_prop(target):
    for _ in range(70):
        try:
            current = json.loads(Path(str(BOT_CONTROL) + '.status').read_text())['host']['prop']
        except (FileNotFoundError, ValueError, KeyError):
            time.sleep(0.3)
            continue
        if current == target:
            return
        command('button 1')
        time.sleep(0.7)
    raise RuntimeError(f'Could not select prop {target}')

def screenshot(name):
    path = OUTPUT / 'screenshots' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['grim', str(path)], check=True)
    print(path, flush=True)

def record(name, seconds, events=()):
    raw = WORK / 'raw' / name
    raw.parent.mkdir(parents=True, exist_ok=True)
    log = open(raw.with_suffix('.log'), 'w')
    recorder = subprocess.Popen(['gpu-screen-recorder', '-w', 'eDP-1', '-f', '30',
        '-k', 'h264', '-q', 'very_high', '-cursor', 'no', '-a', 'default_output',
        '-o', str(raw)], stdout=log, stderr=log)
    start = time.monotonic()
    try:
        for at, callback in events:
            time.sleep(max(0, start + at - time.monotonic()))
            callback()
        time.sleep(max(0, start + seconds - time.monotonic()))
    finally:
        recorder.send_signal(signal.SIGINT)
        recorder.wait(timeout=15)
        log.close()
    if recorder.returncode != 0 or not raw.exists():
        raise RuntimeError(raw.with_suffix('.log').read_text())
    print(raw, flush=True)

def start():
    WORK.mkdir(parents=True, exist_ok=True)
    CONTROL.write_text('0 wait\n')
    Path(str(CONTROL) + '.status').write_text('')
    env = os.environ.copy()
    env['HS_SHOWCASE_CONTROL'] = str(CONTROL)
    profile = WORK / 'profile'
    log = open(WORK / 'game.log', 'w')
    game = subprocess.Popen(['/home/sam/Downloads/Dusklight.AppImage', '--user-dir', str(profile),
        '--mods', str(profile / 'mods'), '--load-save', '1', '--stage', 'F_SP103,0,13,-1',
        '--log-level', 'info'], env=env, stdout=log, stderr=log, start_new_session=True)
    log.close()
    until = time.monotonic() + 45
    while time.monotonic() < until:
        if game.poll() is not None:
            raise RuntimeError((WORK / 'game.log').read_text()[-3000:])
        if state().get('room'):
            break
        time.sleep(0.2)
    room = state()['room']
    bots(take=1, hunt=False, taunts=False, props=[0, 3, 7, 2, 9, 11])
    log = open(WORK / 'bots.log', 'w')
    bot_process = subprocess.Popen(['node', str(ROOT / 'tools/showcase/bots.mjs'),
        '--room', room, '--count', '6', '--control', str(BOT_CONTROL),
        '--ground', str(CONTROL) + '.ground'], stdout=log, stderr=log, start_new_session=True)
    log.close()
    (WORK / 'processes.json').write_text(json.dumps({'game': game.pid, 'bots': bot_process.pid, 'room': room}))
    print(f'Capture session: room {room}, game {game.pid}, bots {bot_process.pid}', flush=True)

def prepare():
    source = Path.home() / '.local/share/TwilitRealm/Dusklight'
    profile = WORK / 'profile'
    (profile / 'mods').mkdir(parents=True, exist_ok=True)
    card = profile / 'USA/Card A'
    card.mkdir(parents=True, exist_ok=True)
    original = json.loads((source / 'config.json').read_text())
    # Copy presentation preferences and this mod's settings; exclude other mods and their data.
    config = {k:v for k,v in original.items() if not k.startswith('mod.') or
              k.startswith('mod.com_i12bp8_hyrule__hide__and__seek.')}
    config.update({'game.pauseOnFocusLost':False, 'game.autoSave':False,
        'game.recordingMode':True, 'game.minimalHUD':True,
        'game.enableAchievementToasts':False, 'game.enableControllerToasts':False,
        'mod.com_i12bp8_hyrule__hide__and__seek.server':'ws://127.0.0.1:8787',
        'mod.com_i12bp8_hyrule__hide__and__seek.control_hints':False,
        'mod.com_i12bp8_hyrule__hide__and__seek.tunic_color':0})
    (profile / 'config.json').write_text(json.dumps(config,indent=2)+'\n')
    save = bytearray((source / 'USA/Card A/01-GZ2E-hyrule-hide-and-seek.gci').read_bytes())
    if bytes(save[8:40]).rstrip(b'\0') != b'hyrule-hide-and-seek':
        raise RuntimeError('Unexpected save identity; refusing to rewrite the copied header')
    # --load-save bypasses the game-mode selector. Adapt only the private copy's CARD filename.
    save[8:40] = b'gczelda2'.ljust(32,b'\0')
    (card / '01-GZ2E-gczelda2.gci').write_bytes(save)
    shutil.copy2(ROOT / 'build-showcase/mods/hyrule_hide_and_seek.dusk', profile / 'mods')
    print(profile)

def stop():
    manifest = WORK / 'processes.json'
    if not manifest.exists():
        return
    processes = json.loads(manifest.read_text())
    for key, signature in [('bots',str(ROOT / 'tools/showcase/bots.mjs')),
                           ('game',str(WORK / 'profile'))]:
        pid = processes[key]
        try:
            actual = Path(f'/proc/{pid}/cmdline').read_bytes().decode(errors='replace')
            if signature not in actual:
                raise RuntimeError(f'PID {pid} no longer identifies our {key} process')
            os.kill(pid,signal.SIGINT)
        except (FileNotFoundError,ProcessLookupError):
            pass
    manifest.write_text(json.dumps({**processes, 'stopped':True}))
    print('Capture game and filming bots stopped.')

def take(name):
    if name == 'hunt':
        command('scene hunt')
        bots(take=time.time_ns(), hunt=False, dance=False, taunts=False,
             positions=[[0,170],[-190,160],[190,160],[-270,-100],[290,-100],[390,100]],
             props=[0,3,7,2,9,11])
        wait_phase(3)
        anchor_bots()
        command('camera 0 750 300 48 0 50')
        time.sleep(3)
        record('04-sword-hunt-raw.mp4', 12, [
            (1, lambda: command('button 512')),
            (2.5, lambda: command('move 0 -0.35')),
            (2.9, lambda: command('move 0 0')),
            (3.5, lambda: command('button 512')),
            (4.5, lambda: command('button 512')),
            (5.2, lambda: screenshot('05-sword-hunt.png')),
            (6.5, lambda: command('button 512')),
            (8, lambda: command('button 4')),
        ])
    elif name == 'props':
        command('scene props')
        bots(take=time.time_ns(), hunt=False, dance=False, taunts=False,
             positions=[[0,-420],[-240,-60],[-110,-100],[130,-90],[240,-60],[320,-160]],
             props=[0,3,7,2,9,11])
        wait_phase(3)
        anchor_bots()
        select_prop(0)
        command('camera 0 950 310 48 0 -160')
        time.sleep(2.0)
        record('01-prop-hunt-raw.mp4', 16, [
            (1, lambda: command('button 8')),
            (2, lambda: command('move -0.6 0')),
            (3, lambda: command('move 0 0')),
            (4, lambda: command('button 8')),
            (5, lambda: command('taunt')),
            (6, lambda: screenshot('02-prop-hunt.png')),
            (7, lambda: bots(hunt=True, speed=180)),
            (15, lambda: bots(hunt=False)),
        ])
    elif name == 'chase':
        command('scene chase')
        bots(take=time.time_ns(), hunt=False, dance=False, taunts=False,
             positions=[[-260,-300],[-330,-80],[-150,-80],[150,-80],[330,-80],[430,-200]])
        wait_phase(3)
        anchor_bots()
        command('camera 0 1100 360 52 0 -50')
        time.sleep(1)
        record('02-hide-and-seek-raw.mp4', 15, [
            (1, lambda: bots(hunt=True, speed=150, dance=True)),
            (1.5, lambda: command('move 0 -0.65')),
            (4, lambda: screenshot('03-hide-and-seek.png')),
            (5, lambda: command('move 0 0')),
            (8, lambda: bots(speed=300)),
            (12, lambda: bots(hunt=False, dance=False)),
        ])
    elif name == 'cats':
        command('scene cats')
        bots(take=time.time_ns(), hunt=False, dance=False, taunts=False,
             positions=[[-80,-220],[-180,-20],[-80,-80],[90,-30],[190,-110],[260,40]], props=[23]*6)
        wait_phase(3)
        anchor_bots()
        select_prop(23)
        command('camera 0 600 225 48 0 -70')
        time.sleep(1)
        record('03-hidden-village-raw.mp4', 13, [
            (1, lambda: bots(dance=True, speed=100)),
            (4, lambda: screenshot('04-hidden-village.png')),
            (7, lambda: command('taunt')),
            (8, lambda: bots(hunt=True, speed=140)),
            (11, lambda: bots(hunt=False, dance=False)),
        ])
    else:
        raise ValueError(name)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=['prepare', 'start', 'stop', 'command', 'bots', 'snapshot', 'record', 'status', 'take'])
parser.add_argument('arguments', nargs='*')
args = parser.parse_args()
if args.action == 'start':
    start()
elif args.action == 'prepare':
    prepare()
elif args.action == 'stop':
    stop()
elif args.action == 'command':
    command(' '.join(args.arguments))
elif args.action == 'bots':
    bots(**json.loads(' '.join(args.arguments)))
elif args.action == 'snapshot':
    screenshot(args.arguments[0])
elif args.action == 'record':
    record(args.arguments[0], float(args.arguments[1]))
elif args.action == 'take':
    take(args.arguments[0])
else:
    print(json.dumps(state(), indent=2))
