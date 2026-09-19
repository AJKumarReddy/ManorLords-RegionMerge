# Building RegionMergeNative.dll

The helper is plain C with no dependencies beyond the Windows API.

## With Zig (no Visual Studio needed)

1. Download Zig from <https://ziglang.org/download/> and extract it anywhere.
2. In this folder, run:
   ```
   zig cc -target x86_64-windows-gnu -O2 -shared -s -o RegionMergeNative.dll region_merge_native.c
   ```
3. Copy `RegionMergeNative.dll` into `ue4ss\Mods\RegionMerge\`.

`build.bat` does step 2 when `zig` is on your `PATH`.

## With Visual Studio (MSVC)

From an *x64 Native Tools Command Prompt*:
```
cl /O2 /LD region_merge_native.c /Fe:RegionMergeNative.dll
```

## Notes

- `signatures.h` holds the first 32 bytes of each game function the helper
  uses, taken from `ManorLords-Win64-Shipping.exe` 0.8.104. The helper
  compares them at startup and does nothing on a mismatch.
- Supporting a new game version means finding the same functions again, then
  updating the addresses in `region_merge_native.c` and the bytes in
  `signatures.h`.
