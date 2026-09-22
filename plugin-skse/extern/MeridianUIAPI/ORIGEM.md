# MeridianUIAPI - origin and how to re-vendor

Third-party headers. **Not authored by this project, and not edited by it.**
The licence ships with them: `LICENSE-MIT`, next to this file.

## Where they came from

| | |
|---|---|
| Files | 18 headers plus `LICENSE-MIT`, 19 in all |
| Copied from | `MeridianUI\SDK\MeridianUIAPI\`, inside an installed Meridian UI - the SDK the mod itself ships |
| Copied on | 2026-09-22 |
| Licence | MIT (`LICENSE-MIT`, kkEngine 2024 and ColdSun 2026) |
| `LibVersion::AS_STRING` | `1.5.0` |
| `APIVersion::AS_STRING` | `1.0` - **this is the one that decides compatibility**, not the one above |

Meridian UI itself is GPL-3.0 and **is not redistributed**: this project ships
only its own DLL and declares Meridian as a required dependency, which is what
the author guide instructs.

## Do not take these from another project

`Lodestone-dll\lodestone-extracted\extern\meridianui-api\` carries `LibVersion`
**1.2.1** and 18 files. Apart from `Version.h`, those 18 are the same content as
these; the difference that matters is the two files that only exist here -
`InputAPI.h` and `InputDllLoader.h`, the `Meridian.Input` extension, which is
the controller path.

**The check is mechanical:** open `Version.h`, read `LibVersion::AS_STRING`. It
must say `1.5.0` or newer, and `InputAPI.h` and `InputDllLoader.h` must be
present. `1.2.1` means the copy came from the Lodestone and has to be redone
from the installed SDK.

## Verifying and re-vendoring

**Compare with line endings normalised**, not raw bytes: the repository stores
and checks out text as LF (`.gitattributes`) while the SDK ships CRLF, so a raw
`sha256` differs for files nobody changed.

```
for f in *; do printf "%s  %s\n" "$(tr -d '\r' < "$f" | sha256sum | cut -d' ' -f1)" "$f"; done
```

Against the installed SDK on 2026-09-22 this matched on all 19 files. Copy the
folder again from the path above when Meridian updates, re-run the comparison,
and update the date and the version in the table.
