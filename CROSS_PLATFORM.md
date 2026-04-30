# Cross-platform workflow (Mac + PC)

This project is built simultaneously on Mac and PC. Each platform regenerates
its own compiled binaries; only **source, content, and configs** travel through git.

## What's in git

✅ Committed:
- `eclipse.uproject`
- `Source/**/*.{cs,h,cpp}`
- `Content/**` (levels, BPs, materials, audio — via Git LFS)
- `Config/*.ini`
- `Plugins/<YourPlugin>/Source/**` and `Plugins/<YourPlugin>/Content/**` (if any)
- Repo docs: `README.md`, `PORT_PLAN.md`, `CROSS_PLATFORM.md`, `.gitignore`, `.gitattributes`

❌ Ignored (regenerated locally per OS):
- `Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`
- `.vs/`, `*.sln`, `*.VC.db`, `vsconfig` (PC / VS)
- `*.xcodeproj/`, `*.xcworkspace/`, `xcuserdata/` (Mac / Xcode)
- `.idea/`, `.vscode/`, `.DS_Store`, `Thumbs.db`

## First-time setup per machine

### Mac

```bash
git clone <repo-url> eclipse
cd eclipse
git lfs install
git lfs pull

# 1. Generate Mac project files
"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" \
  -project="$PWD/eclipse.uproject" -game -engine

# 2. Build the editor target
"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" \
  eclipseEditor Mac Development -project="$PWD/eclipse.uproject" -waitmutex

# 3. Open
open eclipse.uproject
```

### PC

```cmd
git clone <repo-url> eclipse
cd eclipse
git lfs install
git lfs pull

REM 1. Generate VS project files
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\GenerateProjectFiles.bat" ^
  -project="%CD%\eclipse.uproject" -game -engine

REM 2. Build via VS or via CLI
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" ^
  eclipseEditor Win64 Development -project="%CD%\eclipse.uproject" -waitmutex

REM 3. Open
start eclipse.uproject
```

## Day-to-day rules

1. **Pull before opening UE.** Other-platform commits may have changed source files
   that require recompile. UE will auto-recompile on next launch if it detects
   source changes.

2. **`.umap` files are binary and unmergeable.** If two people edit the same level,
   one set of changes will be lost. Coordinate via Slack / "lock" convention before
   touching a level. For Bathroom slice:
   - **Designer** owns all `Content/Levels/*.umap` and `Content/Natalia/*.umap`
   - **Dev** stays out of `.umap` files

3. **`.uasset` files are also binary.** Same rule. Coordinate ownership of:
   - `Content/Blueprints/` — typically dev
   - `Content/UI/`         — dev (UMG widgets)
   - `Content/Materials/`  — designer
   - `Content/Meshes/`     — designer / artist

4. **Never commit `Binaries/` or `Intermediate/`.** The `.gitignore` should prevent
   this. If git status shows them, the .gitignore isn't being respected — check
   you're in the project root.

5. **Line endings**: `.gitattributes` forces LF on source / config to avoid CRLF
   noise between Mac and PC. If you see whole-file diffs, run:
   ```bash
   git rm --cached -r .
   git reset --hard
   ```

## Cleaning out previously-committed binaries

If `Binaries/`, `Intermediate/`, etc. were committed before `.gitignore` existed
(common for projects bootstrapped on PC), purge them once:

```bash
git rm -r --cached Binaries Intermediate DerivedDataCache Saved .vs *.sln vsconfig
git commit -m "chore: remove OS-specific build artifacts from tracking"
git push
```

After this, both platforms will pull a clean state and regenerate their own.

## When source changes break the other platform

If something on PC adds a new dependency to `eclipse.Build.cs`, the Mac dev pulls
and tries to open UE → editor prompts to rebuild. **Click Yes.** It re-runs
`Build.sh` automatically.

If that fails (rare — usually means the new dep doesn't exist on Mac, e.g. a
Windows-only plugin), you'll see a compile error pointing at the Build.cs line.
Either gate that dep with `if (Target.Platform == UnrealTargetPlatform.Win64)`
in Build.cs, or pick a cross-platform alternative.
