# Rebuild-on-pull hooks

ECLIPSE is a C++ project, so the editor has to be compiled on each machine.
Binaries aren't in git (`.gitignore`: `Plugins/*/Binaries/`), which is why a pull
with C++ changes makes Unreal open with "the following modules are missing".

These hooks compile `eclipseEditor` right after the pull instead, so the editor
opens normally. Enable them once per clone, from the repo root:

```
git config core.hooksPath Tools/git-hooks
```

On Windows run that in Git Bash (installed with Git for Windows) — it's also
what runs the hooks.

- Fires after a merge, a rebase or a branch switch, and only when `.cpp`, `.h`,
  `.cs`, `.uproject` or `.uplugin` changed.
- Skips while the Unreal Editor is running (its binaries are locked); it prints
  the command to run once you've closed it.
- Never fails the pull. A broken build prints the last lines and the full log
  lands in `Saved/Logs/PullRebuild.log`.
- Engine expected at `/Users/Shared/Epic Games/UE_5.8` (Mac) or
  `C:\Program Files\Epic Games\UE_5.8` (Windows). Set `UE_ROOT` if it's elsewhere.

The folder also carries Git LFS's own hooks (`pre-push`, `post-commit`, and the
LFS half of `post-checkout`/`post-merge`), because pointing `core.hooksPath` here
replaces the ones LFS installs in `.git/hooks`.

## Requirements

- **Mac**: Xcode command line tools.
- **Windows**: Visual Studio 2022 with the "Game development with C++" and
  "Desktop development with C++" workloads. Unreal drives the compiler, it
  doesn't ship one, so without this no build works — hook or no hook.
- The same engine version on both machines (5.8.x), or every pull rebuilds.
