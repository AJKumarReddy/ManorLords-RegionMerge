@echo off
rem Builds RegionMergeNative.dll with Zig (https://ziglang.org). Run from this folder.
cd /d "%~dp0"
zig cc -target x86_64-windows-gnu -O2 -shared -s -o RegionMergeNative.dll region_merge_native.c || exit /b 1
del /q *.lib *.pdb 2>nul
echo Built RegionMergeNative.dll - copy it to ue4ss\Mods\RegionMerge\
