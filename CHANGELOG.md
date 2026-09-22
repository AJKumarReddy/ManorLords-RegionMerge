# Changelog

## 1.0.7 — 2026-09-21

- **Fixed: forager huts, fisherman's huts and hunting camps on merged land
  produced nothing.** A region's berries, mushrooms, fish, game and stone are
  grouped into resource nodes, and each node names the region it serves. A
  gathering hut takes work only from the nodes of its own region. A hut on
  merged land belongs to the town, but the land's nodes still named the land,
  so the hut found nothing to gather: its family was sent to do chores and the
  hut showed no yield. The land's nodes now serve its town. Seen working in
  game on two saves: a forager and a fisherman's hut on merged land get their
  families out gathering and fishing, and a hunting camp there works too.
  Plot-to-road snapping on merged land still works.
- A save does not store which region a node serves -- it is worked out again
  from where the node stands when the save loads -- so the hand-over is
  repeated on every sweep and nothing new is written into saves.

### Known limitations

- **Merged land reports the goods its town holds, not the none it holds
  itself**, and that total is written to the save (unchanged from 1.0.6).
- A warning raised on merged land is still raised by that land rather than by
  its town, though it carries the town's name (unchanged from 1.0.6).
- Livestock on merged land: residents move to the town on every sweep since
  1.0.6, but no save with a pasture breeding animals on merged land has been
  tried, so this is still unseen.

## 1.0.6 — 2026-09-21

Merged land keeps its own ground and its own roads; what it shares with its
town is the town's, answered where the town is the right answer.

- **Fixed: the region panel on merged land showed nobody and nothing.** 1.0.5
  read that land's own population, wealth and stores -- all of which had moved
  to the town -- so it reported zero people and no treasury. Selecting merged
  land now reads its town, and the panel shows the town it belongs to:
  population, wealth, approval, public order, food and fuel stores, taxes and
  retinue all come from the town it was merged into.
- **A villager on merged land now counts as being in the town they belong to.**
  A villager carries the region they are standing in, and on merged land that
  was the land itself while the building they were sent to belonged to the
  town. The one call that sets it answers with the town now. Whether this is
  what decides a family being dispatched has not been shown either way; it was
  changed because the two disagreeing is wrong on its own terms.
- **Merged land now takes its town's name**, so hovering it names the town
  rather than the settlement it used to be. A save finds a region again by
  where it is, not by what it is called, so a shared name is safe.
- **Livestock** are residents of a region, counted out of the same list as
  people, and they moved to the town once at the merge and never again.
  Residents now move on every sweep, beside the buildings, so anything born or
  bought on merged land afterwards reaches the town too. Untested: no save to
  hand has had a pasture on merged land breeding animals, so this has not been
  seen to do anything yet.
- Plot-to-road snapping, the curve a burgage plot's edge copies, roads and
  construction all work on merged land.
- The helper no longer writes its `ASK` diagnostics to the log.

### Known limitations

- **Buildings that live off the land do not work on merged land.** A forager
  hut, hunting camp or fisherman's hut there produces nothing, while the same
  building off merged land works normally. The family assigned to it has been
  seen both to stay away and to reach the building and stand idle; it is not
  consistent, and it is not understood. A villager of the town told to gather
  merged land's berries does not go. Retagging the deposits to the town was
  tried and did not help. The cause has not been found. Construction and
  haulage on merged land were seen to work; other kinds of building there have
  not been tried.
- **Merged land reports the goods its town holds, not the none it holds
  itself**, and that total is written to the save. Taking the copy away was
  tried: a building placed on merged land then could not be paid for, because
  the cost is checked against the goods the region itself holds, read straight
  out of it with no call to answer for.
- A warning raised on merged land is still raised by that land rather than by
  its town. It now carries the town's name, since merged land takes it, so it
  reads as the town's warning.

## 1.0.5 — 2026-09-20

Merged land now keeps its own place on the map, and what it shares with its
town is shared outright rather than pretended.

- **Fixed: nothing could be built on merged land ("Not enough goods").** A
  building's cost is checked against the goods the region itself holds, read
  straight out of the region with no call to answer for. Merged land now really
  holds what its town holds: both of a region's goods lists are kept as copies
  of the town's, remade every couple of seconds with the game's own routine.
- **Fixed: forager huts, mines and other buildings that live off the land.**
  Resource deposits on merged land were being moved to the town, leaving the
  land with none of its own. They stay where they are now.
- **Fixed: development perks** bought for a town now apply on land merged into
  it, instead of the land answering with perks it never had.
- Roads, plot-to-road snapping and burgage plots on merged land keep working;
  the road lookup behind them is answered by the land that owns the roads.
- Console command `regionmergedebug` reports, while a building is held ready,
  why the game is refusing it and what that land says it holds.

### Known limitation

- The region panel on merged land shows that region's own population rather
  than the town's, so it reads low. The villagers really are working there;
  only the number is wrong.
- Goods taken from merged land's own copy are not taken from the town. Building
  and construction draw on the town, because the buildings belong to it.

## 1.0.4 — 2026-09-20

- **Fixed: roads, plot-to-road snapping and burgage plots on merged land.**
  Placing a burgage plot next to a road on merged land found nothing to snap
  to, and the snap markers did not appear. Everything that asks "which road is
  near here?" works out each road against the region it is given, and merged
  land was answering with its town — whose border those roads fall outside, so
  they were passed over. Merged land now answers for itself, as it does before
  it is merged, and roads there behave as they always did.
- Goods are shared the other way round instead: asked what it holds, merged
  land answers with its town's stock, which is what a building's cost, the
  goods panel and construction read. Buildings on merged land were already
  registered with the town, so the town's stock was always the real pool.
- Villagers of the town work at buildings on merged land, as before.

### Known limitation

- The region panel on merged land shows that region's own population rather
  than the town's, so it reads low. The villagers really are working there;
  only the number is wrong.

## 1.0.3 — 2026-09-19

- The merge hotkey is now **Ctrl+Alt+PageUp**. Ctrl+M also toggled the game's map view
  (the game reacts to its letter keys even with Ctrl held); Page Up is not
  used by the game or the common UE4SS mods. Change it in `config.lua`
  (`MergeKey`, `MergeModifiers`).

## 1.0.2 — 2026-09-19

- Forests and resources on merged land now belong to the town. Woodcutters,
  foresters and other gatherers of the town see the trees on merged land, and
  new buildings there clear the trees under them. Berry, stone and other
  resource patches on merged land are retagged to the town when it merges.
- The merge hotkey can be changed in `config.lua` (`MergeKey`,
  `MergeModifiers`). Note that the game uses M for the map view, so the
  default Ctrl+M also toggles the map.
- Faster: the script no longer scans the game's object table every sweep.
- Tidier native helper: fewer commands, shared helpers, no dead code.

## 1.0.1 — 2026-09-19

- Fixed: merged land did not share resources when building. The UI showed the
  town's goods, but placing or constructing a building on merged land checked
  the empty outpost's stock. The game's own position lookup now treats merged
  land as part of the town, so placement costs, construction, builders and
  storage all use the town.
- Fixed a rare crash a few seconds after saving, caused by the script touching
  buildings the game had already deleted. Building moves now happen entirely in
  the native helper; the script no longer reads the game's building list.
- The HUD follows the town natively, so the script's UI redirect was removed.

## 1.0.0 — 2026-09-19

First release, for Manor Lords 0.8.104.

- Newly claimed regions that border your town join it automatically.
- Ctrl+M / `regionmerge` folds an existing settlement into a neighbouring town
  (families, houses, workplaces, goods, treasury).
- Any number of regions can be combined into one town, including land that
  borders only other merged land.
- Shared population, jobs, storage, treasury and construction.
- The HUD and region panel show the whole town; inner borders are hidden.
- Merges survive saving and loading.
- Free starter goods and wealth are removed from merged land.
- The mod switches itself off on any other game version.
