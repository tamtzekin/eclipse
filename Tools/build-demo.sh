#!/bin/bash
# Package the ECLIPSE demo for Mac → Build/Demo/Mac/eclipse.app
#
# Cooks only L_MainMenu + L_CLUB_NEW1 (Outside-Blockout comes along as the
# club's streaming sublevel). Everything the game string-loads at runtime —
# the Ink story, the WBPs, the DataTables — is covered by DirectoriesToAlwaysCook
# in DefaultGame.ini, not by the cooker's dependency walk.
#
# Pass --build to also recompile the game target (skip it when only content
# changed; the compile is the slow half).
set -euo pipefail

UE="/Users/Shared/Epic Games/UE_5.8"
PROJ="$(cd "$(dirname "$0")/.." && pwd)/eclipse.uproject"
OUT="$(dirname "$PROJ")/Build/Demo"

"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PROJ" -noP4 -platform=Mac -targetplatform=Mac \
  -clientconfig=Development -nodebuginfo -utf8output \
  ${1:+-build} -cook -stage -pak -archive -archivedirectory="$OUT" \
  -map="/Game/Justin/Levels/L_MainMenu+/Game/Justin/Levels/L_CLUB_NEW1"

# UAT's Mac archive step copies Binaries/Mac/eclipse.app — the bare executable,
# with no Contents/UE and therefore no paks. The staged app is the real one.
rm -rf "$OUT/Mac/eclipse.app"
ditto "$(dirname "$PROJ")/Saved/StagedBuilds/Mac/eclipse.app" "$OUT/Mac/eclipse.app"
echo "Demo build: $OUT/Mac/eclipse.app"
