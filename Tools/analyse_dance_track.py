"""Bake a dance track's beat grid, Rekordbox-style.

Run inside the editor (Python console or MCP execute_python):
    exec(open('/Users/j/Documents/Unreal Projects/eclipse/Tools/analyse_dance_track.py').read())
    run('/Game/Audio/dogheadsurigeri_-_Short_Fuse', 'DA_DanceTrack_ClubMusic')
    bake_envelope('DA_DanceTrack_ClubMusic')

1. OnsetNRT scans the waveform offline for kick-drum onsets (20-150 Hz band).
2. Tempo = the BPM whose beat grid the kicks agree with most (phase coherence).
3. Downbeat = which of the 4 beat positions carries the most kick energy.
4. Segment = the densest 60 s run of whole bars.
The result lands in a UEclipseDanceTrackData asset; results also go to Saved/Temp/dance_analysis.txt.
"""
import math
import os
import unreal

DANCE_DIR = '/Game/Justin/Audio/Dance'
OUT_FILE = os.path.join(unreal.Paths.project_saved_dir(), 'Temp', 'dance_analysis.txt')
KICK_MIN_HZ, KICK_MAX_HZ = 20.0, 150.0
BPM_RANGE = (90.0, 180.0)
SEGMENT_SECONDS = 60.0
KICK_KEEP_FRACTION = 0.25


def _write(msg):
    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
    with open(OUT_FILE, 'w') as f:
        f.write(msg)
    unreal.log(msg)


def _load_or_create(name, cls, factory):
    path = f'{DANCE_DIR}/{name}'
    assert path.startswith('/Game/Justin/')
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DANCE_DIR, cls, factory)


def _coherence(times, weights, bpm):
    """How tightly the onsets sit on a grid at this tempo (0..1), and the grid's phase in seconds."""
    period = 60.0 / bpm
    c = s = 0.0
    for t, w in zip(times, weights):
        a = 2.0 * math.pi * t / period
        c += w * math.cos(a)
        s += w * math.sin(a)
    total = sum(weights) or 1.0
    phase = (math.atan2(s, c) / (2.0 * math.pi)) * period % period
    return math.hypot(c, s) / total, phase


def estimate_grid(times, weights, duration):
    """BPM, first downbeat time, and the start bar of the densest SEGMENT_SECONDS window."""
    coarse = max((b / 2.0 for b in range(int(BPM_RANGE[0] * 2), int(BPM_RANGE[1] * 2) + 1)),
                 key=lambda b: _coherence(times, weights, b)[0])
    bpm = max((coarse + d / 100.0 for d in range(-100, 101)), key=lambda b: _coherence(times, weights, b)[0])
    score, phase = _coherence(times, weights, bpm)
    period = 60.0 / bpm

    # Which beat of the bar is the "1": the position with the most kick weight on it.
    per_pos = [0.0] * 4
    for t, w in zip(times, weights):
        n = round((t - phase) / period)
        if abs((t - phase) - n * period) < 0.1 * period:
            per_pos[n % 4] += w
    first_downbeat = phase + per_pos.index(max(per_pos)) * period
    while first_downbeat - 4 * period >= 0:
        first_downbeat -= 4 * period

    bar = 4 * period
    seg_bars = max(1, round(SEGMENT_SECONDS / bar))
    last_start = int((duration - first_downbeat - seg_bars * bar) // bar)
    best_bar, best_w = 0, -1.0
    for b in range(max(0, last_start) + 1):
        lo = first_downbeat + b * bar
        w = sum(wt for t, wt in zip(times, weights) if lo <= t < lo + seg_bars * bar)
        if w > best_w:
            best_bar, best_w = b, w
    return bpm, score, first_downbeat, best_bar, seg_bars


def run(sound_path, track_asset_name):
    sound = unreal.load_asset(sound_path)
    if not sound:
        _write(f'FAIL no sound at {sound_path}')
        return

    sf = unreal.AudioSynesthesiaNRTSettingsFactory()
    sf.set_editor_property('audio_synesthesia_nrt_settings_class', unreal.OnsetNRTSettings)
    settings = _load_or_create('ONSSET_Kick', unreal.OnsetNRTSettings, sf)
    settings.set_editor_property('downmix_to_mono', True)
    settings.set_editor_property('granularity_in_seconds', 0.01)
    settings.set_editor_property('sensitivity', 0.5)
    settings.set_editor_property('minimum_frequency', KICK_MIN_HZ)
    settings.set_editor_property('maximum_frequency', KICK_MAX_HZ)

    nf = unreal.AudioSynesthesiaNRTFactory()
    nf.set_editor_property('audio_synesthesia_nrt_class', unreal.OnsetNRT)
    onset = _load_or_create(f'ONS_{track_asset_name}', unreal.OnsetNRT, nf)
    onset.set_editor_property('settings', settings)
    onset.set_editor_property('sound', sound)   # setting the sound kicks off the analysis

    duration = sound.get_editor_property('duration')
    state = {'ticks': 0, 'h': None}

    # Analysis finishes asynchronously; poll on the Slate tick instead of blocking the bridge.
    def tick(_dt):
        state['ticks'] += 1
        times, strengths = onset.get_channel_onsets_between_times(0.0, duration, 0)
        if not times and state['ticks'] < 600:
            return
        unreal.unregister_slate_post_tick_callback(state['h'])
        if not times:
            _write('FAIL onset analysis produced nothing (is AudioSynesthesia enabled?)')
            return

        # The 20-150 Hz band also catches bass notes; the strongest quarter is the kick pattern.
        cut = sorted(strengths)[int(len(strengths) * (1.0 - KICK_KEEP_FRACTION))]
        kicks = [(t, w) for t, w in zip(times, strengths) if w >= cut]
        bpm, score, downbeat, start_bar, seg_bars = estimate_grid([t for t, _ in kicks], [w for _, w in kicks], duration)

        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.EclipseDanceTrackData)
        track = _load_or_create(track_asset_name, unreal.EclipseDanceTrackData, factory)
        track.set_editor_property('sound', sound)
        track.set_editor_property('bpm', round(bpm, 2))
        track.set_editor_property('first_downbeat_seconds', round(downbeat, 4))
        track.set_editor_property('segment_start_bar', start_bar)
        track.set_editor_property('segment_seconds', SEGMENT_SECONDS)
        for a in (settings, onset, track):
            unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=False)

        _write(f'OK {sound.get_name()}: {bpm:.2f} BPM (grid fit {score:.2f}), '
               f'first downbeat {downbeat:.3f}s, {len(kicks)} kicks of {len(times)} onsets, '
               f'segment bar {start_bar} ({downbeat + start_bar * 240.0 / bpm:.1f}s) for {seg_bars} bars')

    state['h'] = unreal.register_slate_post_tick_callback(tick)


def bake_envelope(track_asset_name, rate=50.0):
    """Loudness envelope for the battle's scrolling waveform, via LoudnessNRT; result in dance_analysis.txt."""
    track = unreal.load_asset(f'{DANCE_DIR}/{track_asset_name}')
    sound = track.get_editor_property('sound')
    nf = unreal.AudioSynesthesiaNRTFactory()
    nf.set_editor_property('audio_synesthesia_nrt_class', unreal.LoudnessNRT)
    loud = _load_or_create(f'LOUD_{track_asset_name}', unreal.LoudnessNRT, nf)
    loud.set_editor_property('sound', sound)
    duration = sound.get_editor_property('duration')
    state = {'ticks': 0, 'h': None}

    def tick(_dt):
        state['ticks'] += 1
        probe = loud.get_normalized_loudness_at_time(duration * 0.5)
        if not probe and state['ticks'] < 600:
            return
        unreal.unregister_slate_post_tick_callback(state['h'])
        env = [loud.get_normalized_loudness_at_time(i / rate) for i in range(int(duration * rate))]
        peak = max(env) or 1.0
        env = [round((v / peak) ** 1.5, 3) for v in env]   # the power widens the gap between kick and breakdown
        track.set_editor_property('envelope', env)
        track.set_editor_property('envelope_rate', rate)
        for a in (loud, track):
            unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=False)
        _write(f'OK envelope {len(env)} samples at {rate}/s, peak {peak:.3f}')

    state['h'] = unreal.register_slate_post_tick_callback(tick)
