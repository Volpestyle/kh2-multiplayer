"""Pinned lossless-frame checks. No native process, game, or decoder operations."""
import hashlib
import math
import re
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageMath


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def ensure(deadline, cancel):
    if cancel.is_set() or time.perf_counter() >= deadline:
        raise ValueError('media artifact deadline/cancellation')


def file(root, name):
    root = Path(root).resolve()
    path = root / name
    if (type(name) is not str or not name or Path(name).is_absolute()
            or any(p in ('', '.', '..') for p in name.replace('\\', '/').split('/'))
            or path.is_symlink() or not path.is_file()
            or not path.resolve().is_relative_to(root)):
        raise ValueError('media artifact path refused')
    # Reject junctions/reparse files, including ancestors below the evidence root.
    for part in (path, *path.parents):
        if part == root:
            break
        if getattr(part.stat(follow_symlinks=False), 'st_file_attributes', 0) & 0x400:
            raise ValueError('media artifact reparse path refused')
    return path


def artifact(root, path):
    path = Path(path)
    return dict(path=path.relative_to(root).as_posix(), bytes=path.stat().st_size, sha256=sha(path))


def frame_metrics(image, baseline):
    """Exact frozen RGB/luma metric; Pillow evaluates arithmetic in native loops."""
    if image.mode != 'RGB' or baseline.mode != 'RGB' or image.size != baseline.size:
        raise ValueError('decoded RGB dimensions/mode changed')
    r, g, b = (band.convert('I') for band in image.split())
    # No expression evaluation; operands use Pillow's integer native loops.
    luma = ImageMath.lambda_eval(lambda a: (a['r']*77+a['g']*150+a['b']*29)>>8,
                                r=r,g=g,b=b).convert('L')
    dr, dg, db = ImageChops.difference(image, baseline).split()
    change = ImageChops.lighter(ImageChops.lighter(dr, dg), db)
    count = image.width * image.height
    return dict(rgbSha256=hashlib.sha256(image.tobytes()).hexdigest(),
                blackFraction=sum(luma.histogram()[:17]) / count,
                changedPixels=sum(change.histogram()[16:]))


def decoder_rows(log, count=90):
    """Parse retained showinfo RGB-frame PTS, never invented index/fps timestamps."""
    timebases = re.findall(r'config in time_base:\s*(\d+)/(\d+)', log)
    if len(timebases) != 1:
        raise ValueError('one decoder time base required')
    numerator, denominator = map(int, timebases[0])
    if numerator <= 0 or denominator <= 0:
        raise ValueError('invalid decoder time base')
    rows = [(int(i), int(pts), int(w), int(h)) for i, pts, w, h in re.findall(
        r'\bn:\s*(\d+)\s+pts:\s*(-?\d+).*?\bs:(\d+)x(\d+)', log)]
    if len(rows) != count or [r[0] for r in rows] != list(range(count)):
        raise ValueError('full exact ninety-frame decode required')
    if any(r[1] < 0 for r in rows) or any(a[1] >= b[1] for a, b in zip(rows, rows[1:])):
        raise ValueError('decoder PTS did not advance')
    return dict(numerator=numerator, denominator=denominator), rows


def summarize(manifest, encoded_fps):
    if type(encoded_fps) not in (int, float) or not math.isfinite(encoded_fps) or encoded_fps <= 0:
        raise ValueError('invalid encoded frame rate')
    rows = manifest['rows']; tb = manifest['timeBase']
    nonblack = [r for r in rows if r['blackFraction'] < .99]
    distinct = len({r['rgbSha256'] for r in nonblack})
    changed = max(r['changedPixels'] for r in rows)
    duration = (rows[-1]['pts'] - rows[0]['pts']) * tb['numerator'] / tb['denominator'] + 1 / encoded_fps
    if abs(duration - 90 / encoded_fps) > 1 / encoded_fps:
        raise ValueError('decoded duration disagrees with native rate')
    return dict(decodedAllFrames=True, decodeErrors=0, decodedFrames=90,
                width=manifest['width'], height=manifest['height'], durationSeconds=duration,
                blackFrames=90-len(nonblack), nonblackDistinctFrames=distinct,
                maximumChangedPixels=changed, motionQualified=distinct >= 2 and changed >= 64,
                motionChannelDelta=16, motionMinimumChangedPixels=64,
                blackLumaThreshold=16, blackPixelFractionThreshold=.99)


def recompute(manifest, root, deadline, cancel):
    """Source can independently recompute every row and retain its own aggregates."""
    ensure(deadline, cancel)
    rows = manifest['rows']
    if type(rows) is not list or len(rows) != 90:
        raise ValueError('ninety retained decoded images required')
    baseline = None
    for index, row in enumerate(rows):
        ensure(deadline, cancel)
        if type(row['index']) is not int or row['index'] != index or type(row['pts']) is not int or row['pts'] < 0:
            raise ValueError('decoded row identity/type invalid')
        name = f"hb_arrival_peer{manifest['instance']}_frames/decoded_{index:03}.png"
        if row['png']['path'] != name:
            raise ValueError('decoded frame file/peer mismatch')
        path = file(root, name)
        if artifact(root, path) != row['png']:
            raise ValueError('decoded frame bytes/hash changed')
        ensure(deadline, cancel)
        with Image.open(path) as source:
            if source.format != 'PNG' or source.mode != 'RGB' or source.size != (manifest['width'], manifest['height']):
                raise ValueError('lossless decoded PNG dimensions/mode mismatch')
            image = source.copy()
        if baseline is None:
            baseline = image.copy()
        actual = frame_metrics(image, baseline)
        ensure(deadline, cancel)
        if any(row[k] != v for k, v in actual.items()):
            raise ValueError('decoded frame metrics mismatch')
        if type(row['changedPixels']) is not int or type(row['blackFraction']) is not float or not math.isfinite(row['blackFraction']):
            raise ValueError('decoded metric strict type mismatch')
        if index and rows[index-1]['pts'] >= row['pts']:
            raise ValueError('decoded PTS regression')
    ensure(deadline, cancel)
    return True
