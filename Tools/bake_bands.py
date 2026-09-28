"""Bass/mid/high envelopes for the dance waveform, computed outside the editor (UE's ConstantQNRT asserts and crashes it).

Shell:  python3 Tools/bake_bands.py              -> Saved/Temp/bands/<key>.json
Editor: import bake_bands; bake_bands.apply()   -> writes envelope_low/mid/high on each DA_DanceTrack_<key>
"""
import array, json, math, os, subprocess, tempfile, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Assets/Audio/Source/Tracks')
OUT = os.path.join(ROOT, 'Saved/Temp/bands')
FS, RATE = 11025, 100   # decode rate; envelope samples per second (matches the loudness Envelope)
TRACKS = {   # key (DA_DanceTrack_<key>) -> source file in Assets/Audio/Source/Tracks
    'ClubMusic': 'dogheadsurigeri - Short Fuse.mp3',
}


def decode(path):
    tmp = tempfile.mktemp(suffix='.wav')
    subprocess.run(['afconvert', '-f', 'WAVE', '-d', f'LEI16@{FS}', '-c', '1', path, tmp], check=True)
    with wave.open(tmp) as w:
        pcm = array.array('h', w.readframes(w.getnframes()))
    os.remove(tmp)
    return pcm


def bands(pcm):
    """One-pole splits at 150 Hz and 2 kHz, RMS per 1/RATE s, each band normalised on its own so quiet highs still show."""
    k1, k2 = 1 - math.exp(-2 * math.pi * 150 / FS), 1 - math.exp(-2 * math.pi * 2000 / FS)
    step = FS / RATE
    lo1 = lo2 = lm = 0.0
    acc = [0.0, 0.0, 0.0]; out = ([], [], []); n = 0; edge = step
    for i, s in enumerate(pcm):
        x = s / 32768.0
        lo1 += k1 * (x - lo1); lo2 += k1 * (lo1 - lo2)   # two poles: kick without the bassline's bleed into mids
        lm += k2 * (x - lm)
        b, m, h = lo2, lm - lo2, x - lm
        acc[0] += b * b; acc[1] += m * m; acc[2] += h * h; n += 1
        if i + 1 >= edge:
            for j in range(3):
                out[j].append(math.sqrt(acc[j] / n)); acc[j] = 0.0
            n = 0; edge += step
    res = []
    for env in out:
        peak = sorted(env)[int(len(env) * 0.995)] or 1.0   # a percentile, so one clipped hit doesn't flatten the rest
        res.append([round(min(1.0, v / peak) ** 1.5, 3) for v in env])
    return res


def apply(keys=None):
    import unreal
    for key in keys or TRACKS:
        path = f'/Game/Justin/Audio/Dance/DA_DanceTrack_{key}'
        da = unreal.load_asset(path)
        data = json.load(open(os.path.join(OUT, key + '.json')))
        n = len(da.get_editor_property('envelope'))
        for prop in ('low', 'mid', 'high'):
            env = (data[prop] + [0.0] * n)[:n]   # same length as the loudness envelope the widget indexes alongside
            da.set_editor_property('envelope_' + prop, env)
        unreal.EditorAssetLibrary.save_loaded_asset(da, only_if_is_dirty=False)
        unreal.log(f'bands {key}: {n}')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for key, name in TRACKS.items():
        low, mid, high = bands(decode(os.path.join(SRC, name)))
        json.dump({'low': low, 'mid': mid, 'high': high}, open(os.path.join(OUT, key + '.json'), 'w'))
        avg = lambda e: sum(e) / len(e)
        print(f'{key}: {len(low)} samples, mean low {avg(low):.2f} mid {avg(mid):.2f} high {avg(high):.2f}', flush=True)
