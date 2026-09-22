# IPluginInterface.h - origin, licence and how to re-vendor

Third-party file. **Not authored by this project, and not edited by it.**

## Where it came from

| | |
|---|---|
| File | `IPluginInterface.h`, 536 lines |
| Copied from | `ModderResource\IPluginInterface.h`, inside an installed RaceMenu Anniversary Edition - the copy the mod itself ships |
| Copied on | 2026-09-22 |
| Upstream author | expired6978 (RaceMenu / SKEE) |
| Upstream project | `github.com/expired6978/SKSE64Plugins` |

## Licence: none is declared, and that is the reason this file exists

`expired6978/SKSE64Plugins` **declares no licence**: `master/LICENSE` answers 404,
and there is no licence file inside `skee64/`. The Nexus page marks the mod as a
"Modder's Resource", and the folder the file comes from is literally named
`ModderResource` - that is the permission this project relies on, and it is not a
licence text.

The decision to use it under those terms is the author's, and is recorded
with the rest of this project's decisions. Nothing here grants anyone else the
right to redistribute the file.

Measured on 2026-09-22, when the copy was taken. Re-check before a release: an
upstream licence appearing later changes what this project may ship.

## Verbatim, and how to prove it

The vendored file is the installed file, unmodified. The types the project needs
are made usable from a wrapper (`src\Core\Skee.h`) that includes this header
inside a namespace, instead of by editing it.

**Compare with line endings normalised**, not raw bytes. The repository stores
text as LF and checks it out as LF (`.gitattributes`), while the installed copy
is CRLF - a raw `sha256` therefore differs for a file nobody changed.

```
tr -d '\r' < IPluginInterface.h | sha256sum
```

| | `sha256` of the LF-normalised content |
|---|---|
| this copy, and the installed copy, 2026-09-22 | `875410dd22230f76a014a6841004e7d346f5fa1c748cfff695f9f495234da277` |

## Re-vendoring

Copy the file again from an installed RaceMenu, and update the hash and the
date in this table.

**Take it from the install, never from another project's vendored copy.**
Measured on 2026-09-22: a copy vendored elsewhere was 505 lines against these
536, missing `kPluginVersion5` and the whole `IFormTagInterface`, while its own
preamble called it a "verbatim copy (diff-confirmed)". A second-hand copy
carries whatever its own vendoring got wrong, and says nothing about it.
