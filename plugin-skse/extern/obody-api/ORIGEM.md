# API.h - origin, licence and how to re-vendor

Third-party file. **Not authored by this project, and not edited by it.**

## Where it came from

| | |
|---|---|
| File | `API.h`, 1419 lines, of which 743 onward are the GPL-3.0 text itself |
| Copied from | `github.com/Aietos/OBody-NG`, path `include/API/API.h` |
| Commit | `e33bbc151c38fbe6feae9f5b04558cd6c7528384` - **tag `4.4.3`** |
| Copied on | 2026-09-22 |
| Upstream author | Aietos (OBody Next Generation) |

## The pin is the INSTALLED version, and that is the whole point

The version installed when this copy was taken is OBody Next Generation
**4.4.3.0**, shipping `SKSE\Plugins\OBody.dll` dated 2025-10-29. The tag `4.4.3` is that version's
source, so this copy is the header for the DLL that will actually answer.

**The previous pin in this project named `91c7813`, and that commit is tag
`4.4.0`, not 4.4.3.** It was recorded at a moment when OBody was not
installed and nothing could be checked against it. Measured here
on 2026-09-22:

```
curl .../91c7813.../include/API/API.h   -> sha256 f049452...
curl .../e33bbc1.../include/API/API.h   -> sha256 f049452...
diff -> identical, byte for byte
```

**The file did not change between 4.4.0 and 4.4.3, so nothing that was
compiled against the old pin was wrong.** The pin still moves to the installed
version, because "the header happens to be identical" is a fact about today and
"the pin names the installed version" is a rule that keeps holding.

## Licence: GPL-3.0, carried inside the file

The header ends with the full GNU GPL version 3 text, from line 743 to the end,
so the licence travels with the file and there is no separate `LICENSE` here to
go stale. This project is **GPL-3.0-or-later** already, imposed by the
vendored CommonLibSSE-NG, so vendoring is licence-clean.

The header states its own intended use, in its own words at the top of the
file: it is meant to be copied wholesale into the consuming project and used
as-is, with no other part of OBody's source involved.

## Verbatim, and how to prove it

The file is upstream's, unmodified. The two game types it deliberately leaves
undefined - `Actor` and `TESForm` - are supplied from outside by the wrapper
`src\Core\ObodyApi.h`, which is what the header itself asks a consumer to do.
**The vendored file is not edited to do that.**

Upstream stores it as LF and this repository stores text as LF
(`.gitattributes`), so here the raw hash and the normalised hash agree:

```
sha256sum API.h
tr -d '\r' < API.h | sha256sum
```

| | `sha256` |
|---|---|
| this copy, and upstream `4.4.3`, 2026-09-22 | `f04945202d9daa6408d7555e46eb2a553a1041ab5d84a8f4efc288168563ea18` |

## Re-vendoring

Download it again from the tag matching the **installed** OBody version and
update the commit, the date and the hash in the tables above. Check
`mods\OBody Next Generation\meta.ini` for that version first - the pin follows
the installed mod, not the newest tag.
