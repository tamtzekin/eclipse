"""Writes each <track>.dance.json saved by the dance sequencer onto DA_DanceTrack_<key>. Run in the editor."""
import glob, json, os
import unreal

FOLDER = os.path.join(unreal.Paths.project_dir(), 'Assets/Audio/Source/Tracks')
NONE = unreal.EclipseDanceStyle.COUNT
for path in glob.glob(os.path.join(FOLDER, '*.dance.json')):
    d = json.load(open(path))
    da = unreal.load_asset(f'/Game/Justin/Audio/Dance/DA_DanceTrack_{d["key"]}')
    if not da:
        unreal.log_warning(f'no track asset for {d["key"]} ({os.path.basename(path)})'); continue
    da.set_editor_property('bpm', d['bpm'])
    da.set_editor_property('first_downbeat_seconds', d['first'])
    da.set_editor_property('segment_start_bar', max(0, d['startBar']))
    da.set_editor_property('switch_bars', d['switchBars'])
    da.set_editor_property('switch_styles', [unreal.EclipseDanceStyle(s) if s >= 0 else NONE for s in d.get('switchStyles', [])])
    unreal.EditorAssetLibrary.save_loaded_asset(da, only_if_is_dirty=False)
    unreal.log(f'sequence {d["key"]}: {len(d["switchBars"])} switches')
