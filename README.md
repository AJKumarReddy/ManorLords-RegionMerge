# RegionMerge — one town across many regions (Manor Lords)

Claim land next to your town and it **becomes part of that town**, instead of a
separate settlement with its own villagers, storage and treasury. You can also
fold an existing settlement into a neighbouring one, and combine as many
regions as you like into a single town.

- **Version:** 1.0.3
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
  land belong to the town too.
- **Correct UI numbers:** the HUD and region panel show the whole town, even
  while you look at merged land. The border line between merged regions is hidden.
- **Save-safe:** merges are stored in your save and survive reloading.
- **Fair:** merged land doesn't get the free starter goods and wealth a new
  settlement would.
- **Version-safe:** on any game version other than 0.8.104 the mod switches
  itself off and the game runs unchanged.

## Installation (short)

1. Install **UE4SS** into `ManorLords\Binaries\Win64`. See [INSTALL.md](INSTALL.md)
   for the tested version and settings.
2. Download `RegionMerge-1.0.3.zip` and **extract it into your Manor Lords
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
  anything it checks the game's code against 0.8.104. If that check passes, it
  makes the game's position lookup treat merged land as the parent town, which
  covers placement costs, construction, builders, storage and the HUD. It also
  re-registers buildings, goods and villagers with the town, including when a
  save loads, and gives the town the merged land's forests.
- **`Scripts/main.lua`**, a UE4SS Lua script. It detects claims, runs merges and
  hides inner borders. Merged land is
  kept as a native *outpost* of its town, so the game saves the link itself.

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
