
# ECLIPSE — game mechanics

Every rule and number the game actually runs on, with the file that owns it.
Design intent lives in [`CLAUDE.md`](CLAUDE.md); tactical work lives in
[`TODO.md`](TODO.md). **This file is the behaviour.**

> Keep it current. Changing a constant or a rule in C++ and leaving this
> file alone makes it a liar, which is worse than not having it. Each
> section names its source file — update both in the same commit.

---

## 1. Meters — Heat and Thirst

`Subsystems/EclipseGameStateSubsystem.{h,cpp}`

Both run 0–10 (`MeterMax`). Heat starts at **8**, Thirst at **5**.

| Band | Rule |
|---|---|
| Critical low | value ≤ **2** — the bar goes white and alarms |
| Critical high | value ≥ **9** |
| Overflow | pushing past 10 |

**Neither meter is a simple "don't let it empty" bar.** High and low both
have uses, and items move them in both directions — see §5.

### Passing out

`OnPlayerDeath` fires only when **Heat and Thirst are both 0**. One empty
meter is a warning: an empty meter's time bleed spills into the other one
instead, so the fail state is always a slow double collapse rather than one
bad drink. Autosave is suppressed while passed out, so RETRY can never load
a dead state.

### Decay — the night is the clock

Time only moves while you are **talking** (see §6), so meters bleed in
conversation and nowhere else. Standing still costs nothing.

| | Interval |
|---|---|
| Heat | −1 per **10** in-game minutes |
| Thirst | −1 per **20** in-game minutes |
| Every NPC conversation | −1 Thirst, once, on close (`ConversationThirstCost`) |
| Failed skill-check choice | −1 Thirst (`ThirstDamageOnFail`) |
| Losing a dance battle | −2 Heat (`HeatLostOnDefeat`) |

Decay advances in whole intervals rather than resetting to "now", so one
long time jump applies every interval it crossed.

### Overflow states

Pushing a meter over 10 puts you in a cooldown, counted in **real** seconds
(game time would be frozen outside dialogue and the state would never end).

| State | Duration | Effect |
|---|---|---|
| **WASTED** (Thirst over) | 45s | anything that would add Thirst is refused (`IsDrinkBlocked`) |
| **OVERHEATED** (Heat over) | 30s | — |

Heat reaching max also takes a one-time Thirst penalty, re-armed when Heat
drops back below max (`bMaxHeatThirstPenaltyApplied`).

---

## 2. Stats

`Subsystems/EclipseGameStateSubsystem.{h,cpp}`

Four stats, all starting at **1**:

| Stat | What it is |
|---|---|
| **Aesthetics** | taste, fit, presentation |
| **Rhythm** | flow, timing, beat-sense |
| **Zen** | composure, silence |
| **Psychedelics** | perception, openness to weird input |

**Learn by doing.** Clicking a skill-check dialogue option grants **20 XP**
(`SkillXP`) to that stat *whether it passes or fails* — using a skill is how
you get better at it. **100 XP** levels it up (`StatXPToLevel`), carrying the
overflow. Nothing decays XP. Items can also grant permanent levels directly
(`StatBoost` / `StatBoostLevels`), routed through `ApplyStatDelta`, and those
outlive the item.

---

## 3. Dance styles

`Data/EclipseDanceStyle.h`

Six styles. Each is **heavy** (legs) or **light** (hands), which decides
which attacks it favours, and owns a colour and a right-stick angle.

| Style | Weight | Wheel |
|---|---|---|
| Hakken | heavy — stamping feet | 270° |
| Muzzing | light | 45° |
| Liquid | light — both hands | 0° |
| Tektonik | light | 90° |
| Jumpstyle | heavy | 135° |
| Shuffle | heavy — fast steps | 225° |

**You start with none.** `UnlockedDanceStyles` is a bitfield, `DanceStyleLevels`
is one level per style, and a level above 0 *is* the unlock
(`SetDanceStyleLevel`). Styles level on dance XP at the same 100 XP per level.
Character creation hands out the first ones; the rest are learned from
teachers out in the city.

Adding a style: one enum entry plus one row in `Info()`.

---

## 4. Dance battle

`Subsystems/EclipseDanceSystemSubsystem.{h,cpp}`, HUD in
`UI/EclipseDanceSystemWidget.{h,cpp}`

Everything runs off the song's own playback position, not a world timer, so
every judgement lands where the beat is actually heard. Numbers below are
given at **128 BPM** (beat 0.469s, bar 1.875s) where they derive from tempo.

### 4.1 The three windows

Two keys: **▲ UP** = light (hands), **▼ DOWN** = heavy (legs). You answer
each of his notes with the same weight he threw.

| | Window | Best case | Also possible |
|---|---|---|---|
| **1. Parry** | his beat ±`ParrySeconds` (0.21s) | **PERFECT** inside the core, ±`PerfectSeconds` (0.105s) | GOOD on the edge, WRONG MOVE, TOO EARLY, TOO LATE |
| **2. Miss** | everything else | — | MISS, named TOO EARLY / TOO LATE only when a move is in flight |

**Parrying is the only way to answer him.** There is no call-and-response a
bar later: once his beat has gone past, that move has landed.

- `ParrySeconds` = `min(0.42, beat × 0.45)` and `PerfectSeconds` is half of it.
  **The parry window is deliberately capped below the gap between his beats.**
  A wider window puts one press inside two notes at once, and the
  nearest-note match then starts answering his pattern backwards.
- Your press is credited `PressOffset` (0.12s, live via
  `eclipse.Dance.PressOffset`) earlier than the clock, since what you hear
  and see lags it. Calibrate with `eclipse.Dance.LogPresses 1` and read the
  offsets back — guessing is not calibration.
- Answering with the wrong weight is **WRONG MOVE**: it consumes the note,
  costs more stance than letting it through, and he names the limb you
  should have used.
- A move is "in flight" from the moment his tell starts until the last of
  it goes unanswered past the reply window. **Throwing while he's just
  strafing is a MISS** — there is nothing to be early or late for, so it is
  never named one.
- Out of his reach (facing error over 30°) is **OUT OF RANGE** — nothing
  you throw can land.

### 4.2 Damage and scoring

| Grade | Points | Dance XP | STANCE damage |
|---|---|---|---|
| PERFECT | 300 | 25 | `NoteDamage × 1.4` |
| GOOD | 100 | 10 | `NoteDamage × 0.6` |
| MISS / TOO EARLY / TOO LATE | 0–50 | 0 | 0 |

`NoteDamage` is **9**, before multipliers:

- **Same style as his** → ×2 (`StyleMatchBonus`), and the HUD flashes SAME STYLE x2
- **HEAT mode** → ×1.5 (`HeatDamage`), and double score
- Answering a whole pattern, every note a PERFECT → a bonus pass of
  `NoteDamage` per note and MOVE MATCHED

### 4.3 Stance — both bars

Yours fills as you make mistakes; his empties as you land on him. Both are
drawn as the same gold bar.

| | Max | Fills on | Recovers |
|---|---|---|---|
| **Yours** | 100 | letting a note through +4, near miss +3, wrong weight +6, outside the oval +2.5/sec | at **5/sec**, but **only while you're inside the oval** |
| **His** | 260 | — | after **3s** untouched, at **7/sec**, plus a full **+10** each time you let a move land unparried |

**Matching his stance is what gets your footing back** — not time passing.
Drift out of the oval and yours stays where it is while his keeps climbing.

Filling yours is **the end of the run**: you collapse, the screen **whites
out**, the deck drags to a halt over 7 seconds, and the overlay offers
**REPLAY** (the autosave) or **END** (the main menu). Emptying his **breaks
his stance** and wins the battle there and then; the bar fades out where it
stands.

### 4.4 HEAT mode

**6 PERFECTs in a row** (`HeatStreak`) lights it for **8 bars**. Everything
hits 1.5× and scores double, and the track speeds up 12%. Only PERFECTs
build the streak — a GOOD or a reply resets it to zero.

### 4.5 Movement and facing

You and your opponent orbit one shared pivot, 70–95cm out, so circling him
is the whole of the footwork.

| | Value |
|---|---|
| Your strafe speed | 16°/sec (A/D or left stick) |
| Arc limit | ±55° from where the battle started |
| In and out | 70 cm/sec (W/S), between 55 and 190 cm from the pivot |
| Facing window | 30° off directly-opposite, **and** within 45 cm of his distance |
| His reach | ±34° in the stance lesson, ±18° in later lessons, ±24° in battle |

**Both halves of the oval have to be matched for anything to land**: square
to him across it, at his distance up and down it. Either one off and your
moves read OUT OF RANGE and your stance climbs. The gauge shows both — the
marker slides sideways as you lose his angle and up or down as you step in
or out.

Faster tracks move him further and quicker, up to 1.7× at 180 BPM. **He only
drifts between moves** — once his telegraph starts he plants himself through
to the end of his pattern, so movement never fights the rhythm. You are
planted too while your own move is still playing or while staggered.

The camera holds the arena rather than tracking the pair, which is what makes
the strafing read as movement at all; both dancers orbiting a shared pivot
under a tracking camera looks like neither of them moving.

### 4.6 Tempo and the deck

The song is a record. `SpinUpSeconds` 3.5s ramps the pitch from
`PitchFloor` 0.4 up to 1 at the start. Dancing well creeps the tempo up to
+8% (`MaxSpeedUp`); dancing badly settles it back. The pause menu slows the
deck and drags it out. Winning rides the track out; losing brakes it,
stutters on a scrap of what you just heard, and dies.

### 4.7 The tutorial — first battle only

Four phases, each held until it's passed, so a lesson can't be outrun.
`bDanceTutorialDone` on the game state skips all of it afterwards.

| Phase | Objective | Passed by |
|---|---|---|
| **Intro** | — | 4 bars of trash talk |
| **Stance** | *Keep your balance* | **4s** unbroken facing him (`HoldFacingToPass`), and never shorter than **8 bars** |
| **Moves** | *Match him, move for move* | copying **2** of his patterns cleanly (`LessonPatterns`) |
| **Lock** | *Read him faster* | the telegraph halves — 2 bars' warning becomes 1 |
| **Battle** | — | 80 seconds, scored |

During the lesson his telegraph runs **8 beats** ahead; in the battle it's
**4**. He throws a pattern every 4 bars while teaching, every 2 in battle.
Style switching is hidden for the whole tutorial — there is nothing to switch
to while he's teaching the basics.

He coaches throughout, and **all of his copy lives in Ink**, read from
`<dialogue_id>_battle_*` knots by `ReadKnotLines`:

| Knot | What it says |
|---|---|
| `_battle_intro` / `_battle_intro_again` | trash talk over the intro bars |
| `_battle_strafe` | a line every two bars through the stance lesson; the first line is his opener |
| `_battle_face` | nags while you're not squared up — 1.2s off him, then at most every 2.2s |
| `_battle_steps` | the three HUD objectives, in order |
| `_battle_lesson` | after each pattern: copied it / kept up but sloppy / fluffed it, in order |
| `_battle_lock` | the handover to reading him live |
| `_battle_both` | once footwork and moves are both in play |
| `_moveset` | his patterns, one per line |
| `_wrong_heavy` / `_wrong_light` | the limb you should have used |
| `_defeated` | what he says once you've beaten him |

The C++ wording stays as a fallback, so an opponent with no Ink still works.
**`_battle_steps` and `_battle_lesson` are read by index — line order is the
contract.**

### 4.8 Moveset notation

One pattern per line in the `_moveset` knot. `L` is light (hands), `H` is
heavy (legs). The character **after** a note sets the gap to the next one:

| | Gap |
|---|---|
| space (or end) | 1 beat |
| `-` | ½ beat |
| `=` | ¼ beat |
| `*` | ⅔ beat (first and third of a triplet) |

`L H L L` is four crotchets — UP DOWN UP UP. Patterns run in order during
the lesson and shuffle in the battle.

**He does not throw every bar.** After a pattern he rests its own length
plus **1–2 bars**, and goes back to strafing for them — the gap is where you
reply, and where his movement is readable. In the lesson he stays on a
predictable 4-bar loop instead.

Each move wears **its own telegraph**, picked by its lead limb: a light lead
is a quick shimmy left and right that grows into the beat, a heavy lead is a
slow bend over the feet with the whole body swinging round. These are
procedural poses, not animations; they are the placeholder until there are
real per-move anims.

### 4.9 Winning and losing

**Breaking his stance is the only win.** Running the track out is a loss —
there is no points victory. The result goes to Ink as `dance_won` and
`<id>_dance_won`, and Ink owns the consequences (Patience is an Ink LIST).
A win queues his `_defeated` lines, which play once the battle camera leaves.

---

## 5. Items

`Data/EclipseItemDefinition.h`, `Subsystems/EclipseGameStateSubsystem.cpp`,
`UI/EclipseInventoryWidget.cpp`

### Types

| Type | Behaviour |
|---|---|
| **Usable** | consumed on USE; applies its effect once |
| **Equippable** | worn until unequipped; modifiers apply continuously |
| **Key** | held, never consumed; dialogue reads it via `QuestFlag` |

An item with a `DialogueId` (the phone) **opens its knot** on USE instead of
being consumed.

### Carrying

Items are **Small** (pocketable) or **Large** (hands only).

| Carrier | Capacity |
|---|---|
| Hands | 1 |
| Pockets | 2 |

Worn clothing occupies body slots instead: Head, Eyes, Neck, Top, Bottom,
Shoes.

### Use effects

Signed deltas on the 0–10 meter scale — **a plus is not automatically good.**
Water `+2 Thirst`; beer `+2 Thirst, −1 Heat`; gum `−1 Thirst` (dries the
mouth); Molly `+1 Heat` (sweat). Items may also carry a permanent
`StatBoost`.

Using an item flashes whichever carrier holds it and consumes it **0.55s
later** — an item that vanishes on the frame you click reads as the click
having missed.

Sounds are picked from the item rather than configured: cigarettes by id,
anything that hydrates you as a drink, and water-bottle versus glass-bottle
off the mesh the row already names.

### Cigarettes

A **currency**, not a chip — spent in bulk (20 for a favour) and shown as a
count. Ink is allowed to write `cigarettes` directly, so trades are plain
Ink arithmetic (`~ cigarettes -= 20`) and the subsystem reconciles the
difference.

---

## 6. Dialogue, time and quests

`Subsystems/EclipseDialogueSubsystem.{h,cpp}`,
`UI/EclipseDialogueWidget.{h,cpp}`

### Skill checks

Choice text carries `[STAT:N]`. Failing one still grants XP and costs
**−1 Thirst**. Ink's own numeric gates (`{rhythm > 3}`) are pushed the real
values before evaluation, so they agree with the C++ checks.

### Time

The chapter clock **only advances in conversation**: **300 game-seconds**
per continuing choice (`DialogueChoiceSeconds`). Exit choices pay nothing.
`ClockScale` multiplies real seconds into game seconds. The HUD shows H:MM
in 10-minute steps, so the night reads as slipping away in blocks rather
than ticking.

This is the central economy: **talking is what costs you the night, and the
night is what drains you.**

### Captions

Lines print word by word, grouped into one closed-caption box per sentence.
Each box ticks as it lands, so a three-sentence line reads as three beats.
Speaker names appear with their words, never before. E / Enter / Space
means "hurry up" while anything is still printing, and only means "pick"
once it's done.

### Side quests

Held in an Ink `SideQuests` LIST and polled into the HUD. A quest leaving
the list means it's finished: the row strikes through, glows, holds, and
fades. A new quest rises into place with a chime. **The newest still-open
quest breathes**, so the thing you were last sent to do is the thing lit up.
Quests already in a loaded save stay silent.

> Ink story state (including `SideQuests`) is **not** in the save yet.

---

## 7. Interaction

`Subsystems/EclipseInteractSubsystem.cpp`, `NPC/EclipseNpcCharacter.cpp`

Nearest talkable and nearest pickup are found per frame, honouring each
NPC's `TalkRadius` (2m default). The line-of-sight gate tries the chest
first and then the head, so a counter between you and someone doesn't make
them unreachable.

Barks appear over the head, caption-styled, 60cm up — or at mouth height
while dancing. Each one clicks as it lands.

---

## 8. Character creation

`UI/EclipseCharacterCreationWidget.{h,cpp}`

**10 points** (`StartingPoints`), max **5** per card (`MaxLevel`). Cards
cover the dance styles and the four stats. Hakken and Muzzing are takeable;
Liquid, Tektonik, Jumpstyle and Shuffle are **locked** — those have to be
found out in the city, and nothing unlocks them yet. Clicking a card shows
its blurb; UPGRADE spends a point.

`eclipse.SkipCreation 1` bypasses it on NEW GAME.

---

## 9. Save

`Save/EclipseSaveGame.{h,cpp}` — see also `CLAUDE.md` §9.

**Autosave only, one slot.** Players live with their decisions. Autosaves on
conversation close, item pickup, pause-menu QUIT, app shutdown, and the
dialogue `startGame` action. Never while passed out.

---

## 10. Controls

| Key | Action |
|---|---|
| W/A/S/D | move — in a dance battle **A/D circle him and W/S close or back off** |
| Mouse / trackpad | look (smoothed over `LookSmoothSeconds`, 0.08s) |
| E | talk / pick up / advance dialogue |
| ▲ / ▼ | light / heavy attack in a dance battle |
| H or left shoulder | cycle dance style (hidden during the tutorial) |
| Right stick | point straight at a style on the wheel |
| TAB | outline nearby interactables |
| I / C / P | inventory / stats / phone |
| Esc | pause |

---

## 11. Console variables

| CVar | Default | What it's for |
|---|---|---|
| `eclipse.Dance.PressOffset` | 0.12 | seconds a press is credited early; raise if presses read TOO LATE |
| `eclipse.Dance.LogPresses` | 0 | log every press with its signed offset, for calibrating the above |
| `eclipse.SkipCreation` | 0 | skip character creation on NEW GAME |
