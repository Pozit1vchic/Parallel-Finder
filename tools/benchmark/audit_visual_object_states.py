"""Collect separate per-frame object observations from a loopback vision model.

This is development evidence, not actor identity, continuity proof or an action
classifier. Keep temporal aggregation separate from the model's captions.
"""
import argparse
import base64
import hashlib
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path

PROMPT = (
    'Inspect the actual visible head covering in this single image. '
    'A hood is fabric attached to a sweatshirt/jacket and pulled over the head. '
    'A flat cap, baseball cap, beanie, or hat is not a hood. '
    'Do not classify an item merely because something is on the head. '
    'If several people are visible and the subject is ambiguous, use unknown. '
    'Return a JSON object with exactly head_covering chosen from hood, cap, hat, '
    'none, unknown; phone chosen from ear, hand, absent, unknown; and evidence '
    'containing one short visible description. No motion/action/identity claims.'
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--model-sha256', required=True)
    parser.add_argument('--projector-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    url = urllib.parse.urlparse(args.url)
    if (url.scheme != 'http' or url.hostname not in ('127.0.0.1', 'localhost', '::1')
            or url.username or url.password or url.query or url.fragment):
        raise ValueError('Only a loopback model endpoint is allowed')
    for digest in (args.model_sha256, args.projector_sha256):
        if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
            raise ValueError('Expected lowercase SHA-256 model fingerprint')
    cases = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    if not isinstance(cases, list) or not 1 <= len(cases) <= 64:
        raise ValueError('Expected 1..64 explicitly selected cases')
    # Validate all files before starting any expensive inference.
    for case in cases:
        if not isinstance(case.get('id'), str) or not 1 <= len(case['frames']) <= 64:
            raise ValueError('Invalid case')
        for name in case['frames']:
            if not Path(name).is_file():
                raise FileNotFoundError(name)
    args.output.mkdir(exist_ok=False, parents=True)
    observations = {}
    rows = []
    prompt_sha = hashlib.sha256(PROMPT.encode()).hexdigest()
    for case in cases:
        row = {'id': case['id'], 'frames': []}
        for name in case['frames']:
            path = Path(name)
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if digest not in observations:
                image = 'data:image/jpeg;base64,' + base64.b64encode(data).decode('ascii')
                body = json.dumps(dict(messages=[
                    {'role': 'system', 'content': PROMPT},
                    {'role': 'user', 'content': [
                        {'type': 'image_url', 'image_url': {'url': image}}]}],
                    temperature=0, seed=12345, max_tokens=160,
                    response_format={'type': 'json_object'})).encode('utf-8')
                request = urllib.request.Request(args.url.rstrip('/') + '/v1/chat/completions',
                    data=body, headers={'Content-Type': 'application/json'})
                began = time.monotonic()
                with urllib.request.urlopen(request, timeout=120) as response:
                    completion = json.load(response)
                raw = completion['choices'][0]['message']['content']
                try:
                    result = json.loads(raw)
                    if (set(result) != {'head_covering', 'phone', 'evidence'}
                            or result['head_covering'] not in {'hood', 'cap', 'hat', 'none', 'unknown'}
                            or result['phone'] not in {'ear', 'hand', 'absent', 'unknown'}
                            or not isinstance(result['evidence'], str)):
                        raise ValueError('Invalid object-state response')
                except (ValueError, TypeError):
                    result = {'head_covering': 'unknown', 'phone': 'unknown', 'evidence': ''}
                    valid = False
                else:
                    valid = True
                observations[digest] = {'imageSha256': digest, 'prediction': result,
                    'valid': valid, 'raw': raw, 'seconds': time.monotonic() - began}
                # Save each observation immediately; interrupted runs keep evidence.
                (args.output / (digest + '.json')).write_text(
                    json.dumps(observations[digest], ensure_ascii=False, indent=2), encoding='utf-8')
            row['frames'].append({'image': str(path.resolve()), 'imageSha256': digest,
                                  'prediction': observations[digest]['prediction']})
        rows.append(row)
        print(case['id'], [f['prediction']['head_covering'] + '/' + f['prediction']['phone']
                          for f in row['frames']], flush=True)
    (args.output / 'report.json').write_text(json.dumps({
        'scope': 'Development per-frame object predictions; no continuity, actor identity or action admission',
        'modelSha256': args.model_sha256, 'projectorSha256': args.projector_sha256,
        'prompt': PROMPT, 'promptSha256': prompt_sha, 'cases': rows,
        'uniqueImages': len(observations)}, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
