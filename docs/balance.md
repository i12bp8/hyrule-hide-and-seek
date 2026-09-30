# Balance changes in v0.3.1

The previous default round awarded a survivor 18 passive points plus 5 for surviving, before
manual taunts, 3-point treasure, the 5-point pickup challenge and 5-point Treasure Rush pickups.
A hunter earned only 5 per find. Loot also spawned only 650–1,800 units from players, with a
2,500-unit host proximity requirement: an empty region could never get treasure.

## References

- [Call of Duty: Black Ops Cold War's official Prop Hunt guide](https://www.callofduty.com/au/en/blog/2020/12/black-ops-cold-war-prop-hunt-mode-spotlight-guide)
  uses a hiding head start, 30-second whistles, decoys and a time limit for hunters to eliminate
  all props. Props also have limited shape changes and a stun. The useful lesson here is to give
  hunters intermittent information and give props time and tools to respond between clues.
- [Fortnite's official Prop Hunt description](https://www.fortnite.com/news/prop-hunt?lang=en-US)
  gives props a survival objective and hunters an elimination objective within the same timer.
- [Epic's UEFN Prop Hunt template](https://www.fortnite.com/news/create-a-prop-hunt-game-with-uefn)
  and [gameplay setup](https://dev.epicgames.com/documentation/fortnite/prop-hunt-10-customizing-the-gameplay-in-unreal-editor-for-fortnite?lang=en-US)
  use a heartbeat for props that remain stationary. That supports stationary clues as pressure
  to relocate, rather than awarding unlimited points for staying in one profitable corner.

These are design references, not evidence that Twilight Princess maps should copy their exact
timings or team sizes. None establishes equal difficulty for this mod. The score budgets below
are our design choice; win-rate balance must be assessed in play.

## Applied rules

Both starting roles can earn at most 12 objective points, 6 bonus points and 6 win points.
Hiders earn their objective points in proportion to elapsed hunt time. Every confirmed capture
awards all starting hunters the newly earned fraction of their 12-point objective budget. This
supports cooperation and avoids making the scoreboard depend on how many targets each hunter
can personally take in a large lobby. Integer rounding is cumulative, so the full capture earns
exactly 12 objective points even when the starting hider count does not divide 12.

Personal finds earn 3 bonus points; a round with one starting hider awards 6 for that find so a
two-player game offers both roles the full budget. Hider treasure gives 1 point (2 every third
round), with up to 2 more at the third pickup. Manual taunts give 1 at most every 10 seconds.
These optional actions share a 6-point gross allowance. Buying decoys reduces points kept, not
points already earned, so spending cannot restart farming. Automatic clues earn no points.

Found hiders can still help hunt, using their remaining personal bonus allowance. They do not
receive starting-hunter progress or the hunter win award, so deliberately getting found cannot
combine both roles' objective or win rewards. Late joiners have no starting role until the next
round. Starting roles, objective counters, bonus counters and capture progress travel with the
round snapshots and survive host migration. Custom hunt durations keep the same maximum score.

The 30-second hiding head start, map-aware hunter counts, hunter tracking, miss cost and three
free decoys remain recommended. The stock stationary clue moves from 20 to 30 seconds. Regular
clues remain 30 seconds on compact maps and 20 on large maps; the final minute uses 15 seconds,
or 12 for the last hider. Clues still last 3 seconds and share a timer with manual/loot clues.
This reduces the combination of infection, tracking and nearly continuous endgame reveals.

Treasure capacity rises from 8 to 24 with 1,200-unit horizontal spacing. Connected ground is
explored during Hide and Hunt, with a bounded native-collision budget per frame. Candidates
favor distance from existing pickups and do not require a nearby player. Floor, slope, wall,
cliff, deep-water, footprint and loading-exit checks run before placement, and placements are checked again
when used. Rupees live 90 seconds to give distant objectives time to be reached. Tight terrain
can support fewer pickups. Rooms whose collision is not loaded cannot be explored until they
load; visited player routes supplement the spawn-connected area, including separate elevations.

## Verification and playtesting

Automated checks cover a large open area with players at the centre, distant routes around a
corner, rejection of an unreachable island, pickup spacing, shared progress for 2–16 players,
short and long round durations, duplicate claims, infection, spending and host migration.
These tests do not validate the real maps' collision or prove a 50% win rate.

For each map, play paired sessions where the same players swap starting roles. Record the
starting hunter/hider counts, side that won, hunt time used, finds, surviving hiders and gross
points before decoy spending. Compare each map and lobby size separately. Aim for roughly equal
wins with comparable players, while avoiding frequent instant captures or whole rounds with no
finds. Do not tune based on a handful of rounds or compare raw team totals with unequal sizes.

If props keep winning, first assess map size and how often clues produce an actual encounter;
try a longer hunt or another starting hunter. If hunters keep winning, try fewer starting
hunters, a longer stationary-clue delay or turning infection off. Keep score rewards comparable
while changing search pressure. The host's Rules tab supports these changes. The new defaults
are a starting point for this playtesting, not a guarantee of equal difficulty.
