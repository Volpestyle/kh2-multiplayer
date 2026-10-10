"""Measured per-local-view storage admission. No allocation or cleanup."""
import hashlib,json,math,shutil
from pathlib import Path


def admit(media_root, *, measurement_path, measurement_sha256, retained_clips,
          margin_fraction=.20, volume_margin_bytes=1024**3, system_root='C:/'):
    if type(retained_clips) is not int or retained_clips not in (1,2):raise ValueError('explicit local retained window count required')
    if type(margin_fraction) is not float or not math.isfinite(margin_fraction) or margin_fraction<.20:raise ValueError('at least20percent measurement margin required')
    if type(volume_margin_bytes) is not int or volume_margin_bytes<1024**3:raise ValueError('at least1GiB media-volume margin required')
    root=Path(media_root)
    if not root.is_absolute() or not root.is_dir():raise ValueError('existing absolute media volume required')
    for part in (root,*root.parents):
        if part.is_symlink() or getattr(part.stat(follow_symlinks=False),'st_file_attributes',0)&0x400:raise ValueError('reparse media volume refused')
    raw=Path(measurement_path).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=measurement_sha256:raise ValueError('independently pinned measurement changed')
    row=json.loads(raw)
    if row.get('ok') is not True or row.get('fullDecodeVerified') is not True or (row.get('width'),row.get('height'),row.get('frames'),row.get('seconds'),row.get('fps'))!=(1920,1080,480,16,30):raise ValueError('qualified1080p480 measurement required')
    peak=row['peakScratchBytes']
    if type(peak) is not int or peak<=0:raise ValueError('positive measured scratch peak required')
    required=math.ceil(peak*(1+margin_fraction))*retained_clips
    system_free=shutil.disk_usage(system_root).free;media_free=shutil.disk_usage(root).free
    if system_free<15*1024**3:raise ValueError('C below15GiB')
    same_volume=root.anchor.casefold()==Path(system_root).anchor.casefold()
    floor=max(volume_margin_bytes,15*1024**3 if same_volume else 0)
    if media_free-required<floor:raise ValueError('measured scratch reserve plus media-volume margin unavailable')
    return dict(mediaRoot=str(root.resolve()),retainedLocalClips=retained_clips,
        measuredPeakBytes=peak,marginFraction=margin_fraction,requiredBytes=required,
        mediaVolumeFloorBytes=floor,systemFloorBytes=15*1024**3,
        observedMediaFreeBytes=media_free,observedSystemFreeBytes=system_free,
        measurementSha256=measurement_sha256,physicalReservation=False,deleteOrArchive=False)
