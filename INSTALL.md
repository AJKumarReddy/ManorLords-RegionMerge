# RegionMerge — installation guide

## Before you start

- **Game version: Manor Lords 0.8.104 only.** On any other version the mod
  switches itself off and the game runs unchanged. The log then says
  `native helper NOT active`.
- **Back up your saves:** copy the folder
  `%LOCALAPPDATA%\ManorLords\Saved\SaveGames` somewhere safe.
- **A save that uses merged regions needs the mod to load.** Keep RegionMerge
  installed for as long as you play such a save.

Your **game folder** is the one that contains `ManorLords.exe`. To find it in
Steam: right-click Manor Lords → *Manage* → *Browse local files*.

## Step 1 — install UE4SS (skip if you already have it)

RegionMerge is a UE4SS mod. It was tested with **UE4SS
`v3.0.1-1136-g35d1795d`** from the `experimental-latest` release.

1. Download `UE4SS_v3.0.1-1136-g35d1795d.zip` (**not** the `zDEV-` file) from
   <https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest>.
   If that exact file is gone, take the newest `UE4SS_v3.x` build. Newer
   versions have not been tested.
2. Extract it into `<game folder>\ManorLords\Binaries\Win64` (the folder with
   `ManorLords-Win64-Shipping.exe`). You should now have `dwmapi.dll` and a
   `ue4ss` folder there.
3. Open `ue4ss\UE4SS-settings.ini` and set these two lines (the tested
   settings):
   ```ini
   bUseUObjectArrayCache = false
   UseModuleOffsets = 1
   ```

## Step 2 — install RegionMerge

**Manual:** extract `RegionMerge-1.0.1.zip` into your **game folder** and let
it merge folders. The result must be:

```
<game folder>\ManorLords\Binaries\Win64\ue4ss\Mods\RegionMerge\enabled.txt
<game folder>\ManorLords\Binaries\Win64\ue4ss\Mods\RegionMerge\RegionMergeNative.dll
<game folder>\ManorLords\Binaries\Win64\ue4ss\Mods\RegionMerge\Scripts\main.lua
<game folder>\ManorLords\Binaries\Win64\ue4ss\Mods\RegionMerge\Scripts\config.lua
```

**Vortex:** install and deploy as usual. Check that the files land in the paths
above.

`enabled.txt` turns the mod on. If you manage mods through
`ue4ss\Mods\mods.txt` instead, add this line above the `Keybinds` line:
```
RegionMerge : 1
```

Windows may block downloaded DLLs. Right-click `RegionMergeNative.dll` →
*Properties*. If there is an *Unblock* checkbox, tick it and press OK.

## Step 3 — check that it works

Start the game and open
`<game folder>\ManorLords\Binaries\Win64\ue4ss\UE4SS.log`. You should see:

```
[RegionMerge] native helper active (OK 0.8.104)
[RegionMerge] loaded (Ctrl+M or console 'regionmerge' merges the selected settlement into its neighbour)
```

If it says `native helper NOT active`, the mod is off (usually a different game
version). Your game is unchanged.

## Using it

- **New land:** claim a region that borders one of your settlements, or land
  already merged into one. A few seconds later it becomes part of that
  settlement. Build there as usual.
- **Existing settlement:** select one of your settlements that borders another
  and press **Ctrl+M**, or type `regionmerge` in the console if you have one.
  It merges into the bordering settlement with the largest population.
- Merged land keeps its own name on the map. Only the border line is hidden.
- Settings: `RegionMerge\Scripts\config.lua` (restart the game after
  editing).

## Uninstalling

- **Saves that never merged anything:** delete
  `ue4ss\Mods\RegionMerge`.
- **Saves with merged regions:** keep the mod while you play them. Without
  it the game puts buildings on merged land back into an empty region, and
  loading can crash. Merges cannot be undone; use your save backup to go back.
- To remove UE4SS completely, delete `dwmapi.dll` and the `ue4ss` folder
  from `ManorLords\Binaries\Win64`.

## Troubleshooting

| Problem | Fix |
|---|---|
| No `[RegionMerge]` lines in the log | Check the folder layout above, and that `enabled.txt` exists (or the `mods.txt` line). |
| `native helper NOT active` | Your game is not 0.8.104, or the DLL is blocked (see *Unblock* above). |
| Ctrl+M does nothing | Select a settlement that borders another of **your** settlements. The log explains why nothing happened. |
| Crash on load | Make sure UE4SS is the tested version, then report the bug with the files below. |

When reporting a bug, attach:
- `ue4ss\UE4SS.log`
- `ue4ss\Mods\RegionMerge\RegionMergeNative.log`
- the newest folder in `%LOCALAPPDATA%\ManorLords\Saved\Crashes`

## Known limits

- Livestock of a merged town (for example its ox) may be counted under
  the old region until you reload the save.
- Tested extensively in an isolated copy of the game, but not yet across a
  long playthrough. Keep backups.
- Placing buildings on merged land by hand was not part of the automated tests
  (they cannot click). The engine side (region lookups, construction
  registration, shared stock) is tested.
- Natural resources (forests, deposits, berry bushes) keep their original
  region tag. If a gatherer on merged land ignores nearby resources, please
  report it.
