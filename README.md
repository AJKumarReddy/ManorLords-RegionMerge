# RegionMerge — one town across many regions (Manor Lords)

Claim land next to your town and it **becomes part of that town**, instead of a
separate settlement with its own villagers, storage and treasury. You can also
fold an existing settlement into a neighbouring one, and combine as many
regions as you like into a single town.

- **Version:** 1.0.7
- **Game:** Manor Lords **0.8.104** (Steam, Windows)
- **Requires:** [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)

## Features

- **Claim and grow:** a newly claimed region that borders your town (or land
  already merged into it) joins the town automatically. Build there as if it
  were your home region.
- **Merge existing settlements:** select one of your settlements that borders
  another and press **Ctrl+Alt+PageUp**. Families, houses, workplaces, goods and
  treasury move into the neighbouring town.
- **Shared everything:** one population, one job pool, one storage, one
  treasury, one construction queue. Forests and resource patches on merged
  land belong to the town too: foragers, fishermen and hunters work there like
  at home, and roads, snapping and burgage plots work on merged land exactly as
  they do at home.
- **Shared storage everywhere:** merged land holds its town's goods, so
  anything you place there is paid for and supplied from the town. Resource
  deposits and development perks work on merged land too. The border line
  between merged regions is hidden.
- **Save-safe:** merges are stored in your save and survive reloading.
- **Fair:** merged land doesn't get the free starter goods and wealth a new
  settlement would.
- **Version-safe:** on any game version other than 0.8.104 the mod switches
  itself off and the game runs unchanged.

## Installation (short)

1. Install **UE4SS** into `ManorLords\Binaries\Win64`. See [INSTALL.md](INSTALL.md)
   for the tested version and settings.
2. Download `RegionMerge-1.0.7.zip` and **extract it into your Manor Lords
   game folder**, the one that contains `ManorLords.exe`. Let it merge
   folders. The mod ends up in:
   ```
   ManorLords\Binaries\Win64\ue4ss\Mods\RegionMerge\
   ```
3. Start the game. `ue4ss\UE4SS.log` should show
   `[RegionMerge] native helper active (OK 0.8.104)`.

Full step-by-step guide, checks, uninstalling and troubleshooting:
**[INSTALL.md](INSTALL.md)**.

## Usage

| Action | How |
|---|---|
| Merge new land | Claim a region that borders your town. It merges within a few seconds. |
| Merge a settlement | Select one of your settlements that borders another, press **Ctrl+Alt+PageUp** (or console: `regionmerge`). |
| Settings | Edit `ue4ss\Mods\RegionMerge\Scripts\config.lua`, then restart the game. |

| Setting | Default | Meaning |
|---|---|---|
| `AutoMergeNewClaims` | `true` | Merge newly claimed neighbouring land automatically. |
| `StripStarterSupplies` | `true` | Remove the free starter goods and wealth that settling gives merged land. |
| `HideInnerBorders` | `true` | Hide the border line inside a merged town. |
| `MergeKey`, `MergeModifiers` | `"PAGE_UP"`, `{ "CONTROL", "ALT" }` | Hotkey for merging a settlement. Avoid letter keys: the game reacts to its own letters even with Ctrl/Alt held. |
| `SweepSeconds` | `2` | How often the mod checks for new claims. |

## Important

- **Back up your saves** before using it:
  `%LOCALAPPDATA%\ManorLords\Saved\SaveGames`
- **A save with merged regions needs the mod to load.** Merges can't be undone,
  and removing the mod from such a save can crash it on load.
- A game update switches the mod off until a matching update is released.

## How it works

Region membership lives in the game's native code, out of reach of Lua. The mod
has two parts:

- **`RegionMergeNative.dll`**, a small helper written in C. Before it changes
  anything it checks the game's code against 0.8.104.
- **`Scripts/main.lua`**, a UE4SS Lua script. It detects claims, runs merges and
  hides inner borders. Merged land is kept as a native *outpost* of its town, so
  the game saves the link itself.

Merged land keeps its own identity on the map. Everything that asks *where* something
is — roads, plot-to-road snapping, burgage plots, borders — gets the land's own
answer, which is why those work there exactly as they do anywhere else.

What is shared is the economy. Buildings on merged land are registered with the
town, so the town's storage is the real pool; asked what it holds, merged land
answers with the town's stock, which is what a building's cost, the goods panel
and construction read. The town's villagers work at buildings on merged land,
and the land's resource nodes (berries, mushrooms, fish, game, stone) serve the
town, so gathering huts there find work.

The full source of the helper is in [`source/`](source); you can build it
yourself (see `source/BUILD.md`). There is no network access, and the helper
only writes its own `.status` and `.log` files next to itself.

## Compatibility

- Tested with UE4SS `v3.0.1-1136-g35d1795d`. Other UE4SS versions are untested.
- Mods that change regions, settlements or claiming may conflict.
- Works alongside UE4SS mods that don't touch regions (console enablers,
  cheat managers and so on).

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE) © 2026 AJKumarReddy. UE4SS and Manor Lords are not covered by
this license.

## Credits

- Author: AJKumarReddy
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) by the UE4SS team
- Manor Lords by Slavic Magic / Hooded Horse. This is an unofficial fan mod.
