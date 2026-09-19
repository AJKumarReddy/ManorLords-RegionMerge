# Changelog

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
