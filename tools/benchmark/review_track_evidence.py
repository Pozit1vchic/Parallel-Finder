"""Decode requested original frames and show observed tracks, not predicted people.

The coloured boxes show admission by the pipeline, not ground-truth identity.
Use a local ignored output directory: this contains actor/frame evidence.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tracks',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--times',type=float,nargs='+',required=True)
    args=parser.parse_args()
    snapshot=json.loads(args.tracks.read_text(encoding='utf-8-sig'))
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=snapshot['source']
    info=json.loads(subprocess.run(['D:/msys2/ucrt64/bin/ffprobe.exe','-v','error',
        '-select_streams','v:0','-show_entries','stream=width,height','-of','json',source],
        capture_output=True,check=True).stdout)['streams'][0]
    inference_width=round(info['width']*min(1.,1280/info['width'],720/info['height']))
    frames=[]
    for i,time in enumerate(args.times):
        path=out/f'{i:03}.jpg'
        decoded=subprocess.run(['D:/msys2/ucrt64/bin/ffmpeg.exe','-hide_banner','-loglevel','info',
            '-ss',str(time),'-copyts','-threads','2','-i',source,'-vf','showinfo,scale=640:-2',
            '-frames:v','1','-n',str(path)],check=True,capture_output=True)
        pts=re.search(r'pts_time:([\d.eE+-]+)',decoded.stderr.decode('utf-8',errors='replace'))
        if not pts or abs(float(pts[1])-time)>.1:
            raise RuntimeError('Original-frame PTS does not match the requested evidence')
        actual=float(pts[1])
        picture=Image.open(path).convert('RGB');draw=ImageDraw.Draw(picture)
        records=[]
        for track in snapshot['tracks']:
            if not track['observations']:continue
            obs=min(track['observations'],key=lambda o:abs(o['time']-actual))
            if abs(obs['time']-actual)>.18:continue
            records.append(dict(track=track['id'],lead=track['sourceLead'],time=obs['time'],
                confidence=obs['confidence'],faceAvailable=bool(obs['face'])))
            factor=picture.width/inference_width
            colour=(70,255,120) if track['sourceLead'] else (255,80,90)
            box=[v*factor for v in obs['box']];draw.rectangle(box,outline=colour,width=2)
            draw.text((box[0],max(0,box[1]-13)),f"track {track['id']} lead {track['sourceLead']}",fill=colour)
            for joint,(x,y,confidence) in enumerate(obs['points']):
                if confidence<.15:continue
                x*=factor;y*=factor
                colour=(80,255,120) if confidence>=.5 else (255,190,65)
                draw.ellipse((x-2,y-2,x+2,y+2),fill=colour)
                draw.text((x+3,y),f'{joint}:{confidence:.2f}',fill=colour)
        frames.append(dict(requestedTime=time,actualTime=actual,image=str(path),observed=records))
        picture.save(out/f'{i:03}-annotated.jpg',quality=92)
    for first in range(0,len(frames),8):
        sheet=Image.new('RGB',(1280,4*394),(18,18,22));draw=ImageDraw.Draw(sheet)
        for j,frame in enumerate(frames[first:first+8]):
            x=j%2*640;y=j//2*394
            sheet.paste(Image.open(out/f'{first+j:03}-annotated.jpg'),(x,y+30))
            draw.text((x+4,y+6),f"Original {frame['requestedTime']:.3f}s; green=admitted / red=rejected",fill='white')
        sheet.save(out/f'page-{first//8:02}.jpg',quality=92)
    (out/'manifest.json').write_text(json.dumps(dict(source=source,frames=frames),indent=2),encoding='utf-8')


if __name__=='__main__':main()
