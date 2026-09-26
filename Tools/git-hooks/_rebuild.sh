#!/bin/sh
# Rebuilds the editor when a pull brings C++ changes, so Unreal doesn't prompt on the next open.
# $1 is a git range ("<old> <new>"). Never fails the pull: a broken build just prints.
[ -n "$1" ] || exit 0
ROOT=$(git rev-parse --show-toplevel) || exit 0
cd "$ROOT" || exit 0
git diff --name-only $1 -- '*.cpp' '*.h' '*.cs' '*.uproject' '*.uplugin' 2>/dev/null | grep -q . || exit 0

case "$(uname -s)" in
    Darwin) PLATFORM=Mac; ENGINE=${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}; BUILD="$ENGINE/Engine/Build/BatchFiles/Mac/Build.sh"; RUNNING=$(pgrep -f 'UnrealEditor' | head -1) ;;
    *)      PLATFORM=Win64; ENGINE=${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}; BUILD="$ENGINE/Engine/Build/BatchFiles/Build.bat"; RUNNING=$(tasklist 2>/dev/null | grep -i UnrealEditor | head -1) ;;
esac

if [ ! -f "$BUILD" ]; then
    echo "[eclipse] C++ changed, but no engine at $ENGINE — set UE_ROOT to your UE 5.8 folder and run: sh Tools/git-hooks/_rebuild.sh '$1'"
    exit 0
fi
if [ -n "$RUNNING" ]; then
    echo "[eclipse] C++ changed — close the Unreal Editor and run: sh Tools/git-hooks/_rebuild.sh '$1'"
    exit 0
fi

mkdir -p "$ROOT/Saved/Logs"
echo "[eclipse] C++ changed — rebuilding eclipseEditor ($PLATFORM), this takes a few minutes…"
if "$BUILD" eclipseEditor $PLATFORM Development -Project="$ROOT/eclipse.uproject" -WaitMutex > "$ROOT/Saved/Logs/PullRebuild.log" 2>&1; then
    echo "[eclipse] build OK — the editor will open without asking."
else
    echo "[eclipse] BUILD FAILED — see Saved/Logs/PullRebuild.log (last lines):"
    tail -15 "$ROOT/Saved/Logs/PullRebuild.log"
fi
exit 0
