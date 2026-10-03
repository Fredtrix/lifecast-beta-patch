# Building Lifecast

This repository holds source only. Building it produces the files a release
archive carries; it does not produce an installed mod on its own.

## Papyrus

`Scripts/Source/*.psc` compiles with the Papyrus compiler that ships with the
Creation Kit. Besides this repository's source folder, the compiler needs the
script sources of everything these scripts extend or call, and none of them are
redistributable here:

| Import | Why |
|---|---|
| `<this repo>\Scripts\Source` | this project's scripts |
| `<Skyrim>\Data\Scripts\Source` or `<Skyrim>\Data\Source\Scripts` | vanilla and SKSE script sources |

```
PapyrusCompiler.exe <script or folder> ^
  -f="TESV_Papyrus_Flags.flg" ^
  -i="<import 1>;<import 2>;..." ^
  -o="<where you want the .pex>"
```

**The game paths differ between installs**, which is why they are written as
placeholders rather than committed as a script. The Creation Kit puts the
vanilla sources in one of the two game folders above depending on how it was
installed - check which one exists. The list is order-independent and the
separator is `;`.

If the mod depends on another framework's scripts, that framework's
`Scripts\Source` joins the `-i=` list, and a row joins the table above.

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
sits inside the mod folder and the compiler writes `.pex` up into it, so the
game loads a build immediately. That arrangement is convenient and entirely
optional: it is one way of pointing `-o=`, and nothing in the source depends
on it.
