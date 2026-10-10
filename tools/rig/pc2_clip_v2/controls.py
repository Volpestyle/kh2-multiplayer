"""Offline schema/metric controls; retained artifacts are fixtures, not a new run."""
import argparse
import copy
import json
from pathlib import Path

from .adapter import read, validate
from . import metrics
from rig.pc2_clip import metrics as legacy_metrics


def run(retained):
    success = read(retained / 'hb_arrival_peer0_clip.json')
    frames = read(retained / 'hb_arrival_peer0_frames.json')
    schemas = {name: read(Path(__file__).with_name(name)) for name in
               ('pc2-owned-clip-v2.schema.json', 'pc2-decoded-frames-v2.schema.json')}
    checks = []

    def refused(label, call):
        try:
            call()
        except (ValueError, TypeError, KeyError):
            checks.append(label)
        else:
            raise AssertionError('not refused: ' + label)

    assert metrics.mode_spec() == (3, 90)
    checks.append('default-3s90')
    for value in ('12s360', '3', '16', '', None, True):
        refused('mode-' + repr(value), lambda: metrics.mode_spec(value))
    for mode, seconds, count in (('3s90', 3, 90), ('16s480', 16, 480)):
        receipt = copy.deepcopy(success)
        receipt['schema'] = 'pc2-owned-clip-v2'
        receipt['request'].update(mode=mode, seconds=seconds, frameCount=count)
        receipt['capture'].update(mode=mode, seconds=seconds, frameCount=count, framesWritten=count)
        receipt['decode']['decodedFrames'] = count
        if count==480:receipt['captureReleaseJournal']=copy.deepcopy(success['rawCliReceipt'])
        schema = schemas['pc2-owned-clip-v2.schema.json']
        validate(receipt, schema)
        checks.append(mode + '-success-shape')
        if count==480:
            bad=copy.deepcopy(receipt);del bad['captureReleaseJournal']
            refused('16s480-missing-release-artifact',lambda:validate(bad,schema))
        # Shape-only synthetic extension: never promoted to actual retained media.
        manifest = copy.deepcopy(frames)
        manifest.update(schema='pc2-decoded-frames-v2', mode=mode, frameCount=count)
        manifest['rows'] = [dict(copy.deepcopy(frames['rows'][i % 90]), index=i, pts=i)
                            for i in range(count)]
        frame_schema = schemas['pc2-decoded-frames-v2.schema.json']
        validate(manifest, frame_schema)
        checks.append(mode + '-frame-shape')
        for path, value in ((('request', 'frameCount'), 480 if count == 90 else 90),
                            (('capture', 'mode'), '16s480' if count == 90 else '3s90'),
                            (('capture', 'seconds'), 12), (('decode', 'decodedFrames'), True)):
            bad = copy.deepcopy(receipt)
            bad[path[0]][path[1]] = value
            refused(mode + '-cross-' + '.'.join(path), lambda: validate(bad, schema))
        bad = copy.deepcopy(manifest)
        bad['rows'].pop()
        refused(mode + '-missing-frame', lambda: validate(bad, frame_schema))
        bad = copy.deepcopy(manifest)
        bad['frameCount'] = True
        refused(mode + '-boolean-count', lambda: validate(bad, frame_schema))
        bad = copy.deepcopy(manifest)
        bad['mode'] = '12s360'
        refused(mode + '-third-mode', lambda: validate(bad, frame_schema))
    short = copy.deepcopy(frames)
    short.update(mode='3s90', frameCount=90)
    assert metrics.summarize(short, success['capture']['encodedFps']) == legacy_metrics.summarize(frames, success['capture']['encodedFps'])
    checks.append('default90-legacy-metrics-equal')
    return dict(passed=len(checks), failed=0, checks=checks,
                limit='Retained 90-frame metrics and synthetic schema shapes; no native capture or 480-frame live claim.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--retained', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.retained)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
