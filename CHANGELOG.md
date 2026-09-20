# Changelog

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
