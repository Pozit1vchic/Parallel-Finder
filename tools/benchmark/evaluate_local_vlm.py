"""Audit a local vision model on ordered frames and temporal controls; no app changes."""
import argparse
import base64
import json
import time
import urllib.request
import urllib.parse
from pathlib import Path

PROMPT = """Classify the visible main person's action in the ordered frames. Do not identify the person.
Use only visible evidence. Distinguish putting a hood ON from taking it OFF by the visible state change.
Hands near the head do not alone prove a hood action or a telephone. A cap is not a hood.
Do not infer motion from identical frames. If the temporal phase is not visible, use unknown or static.
Keep posture, view, and object state separate from the action. Ignore music, captions, and editing effects.
Return one JSON object with exactly these string fields:
action: hood_on, hood_off, hood_adjust, phone_to_ear, look_phone, head_turn, body_turn, walk,
arm_raise, arm_lower, arm_wave, arm_reach, stand_up, sit_down, drink, handle_object, static, other, unknown.
view: front, three_quarter, profile, back, unknown.
posture: standing, sitting, lying, unknown.
hood_state: on, off, changing, unknown.
phone_state: ear, hand, absent, unknown.
evidence: a short description of what is actually visible, including start/end state if changing.
Do not output confidence percentages or guess an invisible object."""
ALLOWED = {
    'action': set('hood_on hood_off hood_adjust phone_to_ear look_phone head_turn body_turn walk arm_raise arm_lower arm_wave arm_reach stand_up sit_down drink handle_object static other unknown'.split()),
    'view': set('front three_quarter profile back unknown'.split()),
    'posture': set('standing sitting lying unknown'.split()),
    'hood_state': set('on off changing unknown'.split()),
    'phone_state': set('ear hand absent unknown'.split()),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    parsed = urllib.parse.urlparse(args.url)
    if parsed.scheme != 'http' or parsed.hostname not in ('127.0.0.1','localhost','::1'):
        raise ValueError('Local audit must use loopback only')
    args.output.mkdir(exist_ok=False)
    cases = json.loads(args.manifest.read_text(encoding='utf-8'))
    results = []
    for case in cases:
        frames = [Path(frame) for frame in case['frames']]
        if not 2 <= len(frames) <= 12:
            raise ValueError('Expected 2..12 explicitly selected frames')
        images = ['data:image/jpeg;base64,'+base64.b64encode(frame.read_bytes()).decode('ascii') for frame in frames]
        row = dict(id=case['id'],frames=[str(frame.resolve()) for frame in frames],results={})
        for mode, ordered in (('forward',images),('reverse',images[::-1]),('static',[images[0]]*len(images))):
            content = [{'type':'text','text':'These frames are in temporal order; classify only their visible sequence.'}]
            for index, image in enumerate(ordered):
                content.extend(({'type':'text','text':f'Frame {index+1}:'},
                    {'type':'image_url','image_url':{'url':image}}))
            body = json.dumps(dict(messages=[{'role':'system','content':PROMPT},{'role':'user','content':content}],
                temperature=0,seed=12345,max_tokens=400,response_format={'type':'json_object'})).encode('utf-8')
            began = time.monotonic()
            request = urllib.request.Request(args.url.rstrip('/')+'/v1/chat/completions',data=body,headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=180) as response:
                completion = json.load(response)
            text = completion['choices'][0]['message']['content']
            try:
                prediction = json.loads(text)
                if set(prediction) != set(ALLOWED)|{'evidence'} or not isinstance(prediction['evidence'],str):
                    raise ValueError('Invalid facet fields')
                if any(prediction[name] not in choices for name,choices in ALLOWED.items()):
                    raise ValueError('Invalid facet class')
                valid = True
            except (ValueError,TypeError):
                prediction = None
                valid = False
            row['results'][mode] = dict(valid=valid,prediction=prediction,raw=text,
                uniqueFrames=len(set(ordered)),seconds=time.monotonic()-began,usage=completion.get('usage'))
            with (args.output/f"{case['id']}-{mode}.json").open('x',encoding='utf-8') as out:
                json.dump(row['results'][mode],out,indent=2)
            print(case['id'],mode,prediction,flush=True)
        results.append(row)
    with (args.output/'report.json').open('x',encoding='utf-8') as out:
        json.dump(dict(scope='Development audit, not held-out accuracy; no actor-identity or pair admission',
                       prompt=PROMPT,cases=results),out,indent=2)


if __name__ == '__main__':
    main()
