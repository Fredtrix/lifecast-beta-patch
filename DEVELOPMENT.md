# Building Lifecast

This repository holds source only. Building it produces the files a release
archive carries; it does not produce an installed mod on its own.

## The SKSE plugin (C++)

`plugin-skse/` builds `Lifecast.dll`, which goes to `Data/SKSE/Plugins/`.

Requirements:

| | |
|---|---|
| Compiler | MSVC, C++23. Visual Studio 2022 with the "Desktop development with C++" workload |
| CMake | 3.21 or newer |
| vcpkg | in manifest mode; `VCPKG_ROOT` must point at your clone |
| Submodule | `plugin-skse/extern/commonlibsse-ng` - CommonLibSSE-NG (`alandtse/CommonLibVR`, branch `ng`), pinned to **v6.7.1** |

```
git submodule update --init plugin-skse/extern/commonlibsse-ng
cmake --preset vs2022-windows -S plugin-skse
cmake --build plugin-skse/build --config Release
```

Always build `Release`. A `Debug` build links the debug CRT, which players do
not have installed, and SKSE then refuses to load the DLL with error `0000007E`
(`ERROR_MOD_NOT_FOUND`). The build prints which configuration it just deployed
for exactly this reason.

**`ENABLE_SKYRIM_VR` is forced `OFF`.** Lifecast does not target VR, and the VR
build would additionally need CommonLibSSE-NG's nested `openvr` submodule.

After a successful build, CMake copies the DLL next to the repository, into
`../../SKSE/Plugins/`. Override that with `-DLIFECAST_OUTPUT_FOLDER=<path>`, or
pass an empty string to skip the copy.

### Vendored third-party headers

`plugin-skse/extern/` carries headers this project did not write and does not
modify. Each folder has an `ORIGEM.md` saying where its contents came from,
when, under which licence, and how to verify the copy is unchanged.

| Folder | What |
|---|---|
| `commonlibsse-ng/` | git submodule, GPL-3.0-or-later. This is what makes Lifecast's own source GPL-3.0-or-later |
| `MeridianUIAPI/` | Meridian UI's public API headers, MIT (`LICENSE-MIT` included) |
| `skee-modderresource/` | RaceMenu's SKEE modder resource header. No licence is declared upstream - see its `ORIGEM.md` |

Meridian UI and RaceMenu themselves are **not** redistributed. They are
required dependencies the player installs.

## The panel

`panel/` is the page Meridian UI draws over the character editor. It has no
build step: the files are written by hand, and installing them is a copy.

Copy the **contents** of `panel/` to `Data/MeridianUI/Lifecast/`, so that
`index.html` ends up at `Data/MeridianUI/Lifecast/index.html`. The DLL opens it
as `mod://Lifecast/index.html` - the host of that URL is the folder name under
`Data/MeridianUI/`, so the folder has to be called exactly `Lifecast`.

A copy does not delete: a file removed from `panel/` stays in the destination
until you remove it there too.

## What the author's own tree does differently, and why you do not need it

The author develops inside a Mod Organizer 2 instance, where this repository
sits inside the mod folder and the build copies the DLL up into it, so the
game loads a build immediately. That arrangement is convenient and entirely
optional: it is one way of pointing `LIFECAST_OUTPUT_FOLDER`, and nothing in
the source depends on it.
