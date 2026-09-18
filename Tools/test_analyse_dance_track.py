"""Offline check of the beat-grid maths: synthetic kicks at known tempos must come back exact. Run: python3 Tools/test_analyse_dance_track.py"""
import os, random, sys, types
sys.modules['unreal'] = types.SimpleNamespace(Paths=types.SimpleNamespace(project_saved_dir=lambda: '/tmp'))
ns = {}
exec(open(os.path.join(os.path.dirname(__file__), 'analyse_dance_track.py')).read(), ns)

random.seed(1)
for true_bpm, true_db in ((128.0, 0.37), (124.5, 1.12), (140.0, 0.05)):
    p, ts, ws, t, i = 60 / true_bpm, [], [], true_db, 0
    while t < 300:
        ts.append(t + random.uniform(-0.005, 0.005)); ws.append(1.0 if i % 4 == 0 else 0.7)   # kick, accented "1"
        ts.append(t + p / 2 + random.uniform(-0.005, 0.005)); ws.append(0.15)                # offbeat hat
        t, i = t + p, i + 1
    bpm, _, db, _, _ = ns['estimate_grid'](ts, ws, 300.0)
    assert abs(bpm - true_bpm) < 0.05, (true_bpm, bpm)
    assert abs(((db - true_db) / p + 0.5) % 4 - 0.5) < 0.1, (true_db, db)
print('beat grid: OK')
