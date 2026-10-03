#!/usr/bin/env python3
"""Export staged in-engine captures as small H.264/AAC Discord clips."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'work/showcase'
OUTPUT = ROOT / 'work/discord-share-2026-10-01'
CLIPS = OUTPUT / 'clips'
CLIPS.mkdir(parents=True, exist_ok=True)
FONT = ROOT / 'dusklight/res/Inter-Bold.ttf'
REGULAR = ROOT / 'dusklight/res/Inter-Regular.ttf'

def run(args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-4000:])

def label_filter(title, subtitle='STAGED BOT SESSION'):
    return (f"drawbox=x=42:y=36:w=420:h=82:color=0x0b1711@0.78:t=fill,"
            f"drawbox=x=42:y=36:w=4:h=82:color=0xc7ad70:t=fill,"
            f"drawtext=fontfile='{FONT}':text='{title}':fontsize=32:fontcolor=0xf4edd9:x=64:y=56,"
            f"drawtext=fontfile='{REGULAR}':text='{subtitle}':fontsize=17:fontcolor=0xf4edd9:"
            "x=w-tw-38:y=h-th-30:shadowcolor=black:shadowx=1:shadowy=2")

def encode(source, output, start, duration, title, maxrate='4200k'):
    vf = ("fps=30,scale=1920:1080,setsar=1,eq=contrast=1.025:saturation=1.045,"
          + label_filter(title) + f",fade=t=out:st={duration - 0.3}:d=0.3")
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-ss',str(start),'-i',str(source),
         '-t',str(duration),'-vf',vf,'-af',f'volume=16dB,afade=t=out:st={duration - 0.3}:d=0.3',
         '-c:v','libx264','-preset','medium','-threads','4','-crf','21',
         '-maxrate',maxrate,'-bufsize','8400k','-pix_fmt','yuv420p',
         '-c:a','aac','-b:a','128k','-ar','48000','-movflags','+faststart',
         '-map_metadata','-1',str(output)])
    print(output.name, flush=True)

shots = [
    ('01-prop-hunt-raw.mp4','01-prop-hunt.mp4',0.6,14.8,'PROP HUNT'),
    ('02-hide-and-seek-raw.mp4','02-hide-and-seek.mp4',0.6,9.5,'PROP CHASE'),
    ('03-hidden-village-raw.mp4','03-cat-disguise.mp4',0.6,11.8,'THAT CAT IS A PLAYER'),
    ('04-sword-hunt-raw.mp4','04-sword-hunt.mp4',0.3,8.8,'HUNT THE PROPS'),
]
with ThreadPoolExecutor(max_workers=2) as pool:
    futures = [pool.submit(encode, WORK/'raw'/src, CLIPS/out, start, duration, title)
               for src,out,start,duration,title in shots]
    for future in futures:
        future.result()

# A short, action-first montage. Hard cuts preserve the native hit and reveal feedback.
segments = [
    ('04-sword-hunt-raw.mp4',0.4,4.0,'THAT POT IS A PLAYER'),
    ('01-prop-hunt-raw.mp4',1.2,7.7,'HIDE. DECOY. ESCAPE.'),
    ('02-hide-and-seek-raw.mp4',1.0,6.0,'OR JUST RUN FOR IT'),
    ('03-hidden-village-raw.mp4',4.0,6.7,'EVEN CATS CAN HIDE'),
]
inputs=[]
filters=[]
duration=0
for index,(source,start,length,title) in enumerate(segments):
    inputs += ['-ss',str(start),'-t',str(length),'-i',str(WORK/'raw'/source)]
    filters.append(f"[{index}:v]fps=30,scale=1920:1080,setsar=1,setpts=PTS-STARTPTS,"
                   "eq=contrast=1.025:saturation=1.045," + label_filter(title) + f"[v{index}]")
    filters.append(f'[{index}:a]aresample=48000,asetpts=PTS-STARTPTS,volume=16dB[a{index}]')
    duration += length
inputs += ['-loop','1','-framerate','30','-t','2.5','-i',str(OUTPUT/'00-cover.png')]
inputs += ['-f','lavfi','-t','2.5','-i','anullsrc=r=48000:cl=stereo']
filters.append('[4:v]scale=1920:1080,setsar=1,format=yuv420p,setpts=PTS-STARTPTS[v4]')
filters.append('[5:a]asetpts=PTS-STARTPTS[a4]')
filters.append(''.join(f'[v{i}][a{i}]' for i in range(5)) + 'concat=n=5:v=1:a=1[v][a]')
run(['ffmpeg','-hide_banner','-loglevel','error','-y',*inputs,'-filter_complex',';'.join(filters),
     '-map','[v]','-map','[a]','-c:v','libx264','-preset','medium','-threads','4','-crf','22',
     '-maxrate','2300k','-bufsize','4600k','-pix_fmt','yuv420p','-c:a','aac','-b:a','112k',
     '-movflags','+faststart','-map_metadata','-1',str(CLIPS/'00-highlight-reel.mp4')])
print('00-highlight-reel.mp4', flush=True)

manifest=[]
for file in sorted(CLIPS.glob('*.mp4')):
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format',
                                            '-of','json',str(file)]))
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert video['width']==1920 and video['height']==1080
    assert video['codec_name']=='h264' and video['pix_fmt']=='yuv420p'
    assert any(s['codec_name']=='aac' for s in probe['streams'])
    assert file.stat().st_size < 9_500_000, f'{file.name} needs a smaller export'
    manifest.append(dict(file='clips/'+file.name, bytes=file.stat().st_size,
                         seconds=round(float(probe['format']['duration']),2),
                         resolution='1920×1080',fps=video['avg_frame_rate']))
(OUTPUT/'media-info.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2),flush=True)
