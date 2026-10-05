# Prop Hunt balance in v0.4.1

## Score budgets

Both starting roles have a **26-point ceiling**: 12 objective, 8 gross bonus and 6 win points.
Survival earns hiders objective points in proportion to the chosen hunt duration. Every confirmed
find advances all starting hunters' shared objective progress: `12 * found / startingHiders`,
rounded down cumulatively. This reaches exactly 12 even with uneven team sizes.

A personal find gives 3 bonus points, or 8 if only one prop started, making the full budget
available in a two-player game. Loot gives 1 point, or 2 every third round, plus up to 2 for a
third pickup. These awards and the following style rewards share the 8-point gross allowance:

| Award | Points | Condition / limit |
| --- | --- | --- |
| Manual taunt | 1 | At least 10 s between scoring taunts |
| Bold taunt | 2 instead of 1 | A living hunter within 1,500 units |
| Decoy fooled | 1 | Hunter strikes your decoy; at most three per round |
| Close call | 1 | Hunter misses within 400 units; at most two, at least 8 s apart |
| Last standing | 2 | Only survivor when time expires; more than one starting prop |
| First blood | 1 | Round's first confirmed find |
| Quick find | 1 | Another personal find within 20 s |

Spending 3 points on a decoy reduces retained points, without resetting the gross allowance.
Automatic clues earn no points. Found hiders keep survival points and can earn personal find
bonuses from their remaining allowance, but receive no starting-hunter objective or win award.
Late joiners help hunt and earn personal bonuses; they join the starting teams next round.

## Search and escape

Recommended rules retain 30 s to hide, 180 s to hunt, three free decoys, infection, five hearts
and a half-heart miss penalty. Auto hunter counts use one per four players on compact maps and
one per three on large maps, rounded up with at least one prop. Random avoids large arenas with
fewer than six players.

- Tracking gives hunters three seconds of direction and rough range every 25 s.
- A hunter can force every hidden prop to make a positional sound every 40 s. It provides no arrows or markers.
- Props can hold Up for 0.45 s to swap with their newest decoy, twice per round, 15 s apart.
- There are no recurring or stationary clues by default. Manual taunts and loot give three-second
  bearing clues. Each remaining prop gives one exact location marker with 20 s left; the host
  can set 0–60 s, with short hunts capped at half their duration.
- A hunter at zero hearts is eliminated. If every hunter is out, props win. Only a confirmed
  find restores one heart, capped at five; vanilla healing cannot refill round health.

Ordinary clues follow the current horizontal bearing, without an exact distance, name or world
marker. The final marker stays at the position revealed. A later pickup cannot erase or prolong
it. Swapping leaves a decoy at the old location, offering an escape without unlimited teleports.

## Arenas and treasure

The 11 arenas are whole maps with only their loading exits closed, standing spawns, prop
palettes and added native scenery. Ordon Ranch, Bulblin Camp and Hyrule Castle Grounds are marked large. Removed open water
and field maps are replaced by more contained outdoor and interior spaces.

Treasure exploration uses loaded native collision and visited player routes. Up to 24 pickups
are separated by 1,200 horizontal units, live 90 s, and exclude collected/expired areas for 30 s.
New pickups stay 600 units from live props. Walls, steep slopes, cliffs, deep water and loading
exits reject candidates. Tight maps can support fewer pickups.

## Authority and verification

The host validates finds, misses, swaps, decoys and pickups. Snapshots carry starting roles,
objective and gross-bonus counters, style limits, health and remaining ability/style cooldowns.
Cooldowns are relative durations, so a migrated host with a different clock preserves them.
Duplicate requests and stale-round packets cannot grant repeated points or extra ability uses.

The rules suite checks budgets for 2–16 players, infection, spending, style caps, two-player
scoring, ability limits, late joins and host migration. The Linux game checks cover rendering,
loading and collision behavior. These checks do not establish equal win rates or validate every
route's treasure reachability. See [the recorded checks and device checklist](testing.md).

For each arena, play paired sessions where the same players exchange starting roles. Record team
sizes, winner, hunt duration, finds, surviving props and gross points before spending. Compare
maps and lobby sizes separately. If props consistently win, assess arena size and actual clue
encounters before adjusting time or hunter count. If hunters consistently win, reduce hunters,
disable the final clue or turn infection off. Keep the score budgets comparable while changing
search pressure.
