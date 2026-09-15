# Validation record — 2026-09-15

## Test base

- Japanese retail clean installation
- NFSPatcher Main Patch followed by No-CD Patch
- Ultimate ASI Loader and NFSMostWanted Widescreen Fix
- No other gameplay MODs in the isolated copies

## Executables

| Variant | Size | SHA-256 | PE LAA |
|---|---:|---|---|
| noLAA | 6,029,312 | `80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C` | false |
| LAA | 6,029,312 | `B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06` | true |

Both files have timestamp `0x438E4C8C` and entry point RVA `0x003C4040`.

## Build and package

- Clean Release/Win32 rebuild: PASS
- Native unit tests: 14/14 PASS
- Public ZIP verifier: PASS
- Public payload: 3 Bridge files
- Validated user-owned resource metadata: 40 entries
- Bundled game resources and executables: 0
- Bundled third-party ASI loader and Widescreen Fix: 0

## Installer and rollback

The public installer was tested independently on both executable variants.

- Exact executable identity accepted: PASS
- Existing Japanese resources: 40 validated, 0 overwritten
- Bridge files created: 3
- Registry changes: 0
- Save-file changes: 0
- Uninstaller removed the 3 Bridge files: PASS
- Uninstaller restored the original WideFix setting: PASS
- Executable SHA-256 unchanged after uninstall: PASS

## Runtime evidence

Each public-package installation was launched in an isolated directory. At 12
seconds both processes were running, responsive, and had the expected game
window title.

Both variants logged successful access to:

- `LANGUAGES\JAPANESE.BIN`
- `LANGUAGES\LANGUAGETEXTURES.BIN`
- `GLOBAL\INGAMEC.BUN`
- `FRONTEND\FRONTA.BUN`
- `FRONTEND\FRONTB.LZC`
- redirected Japanese NTSC PSA movie
- redirected Japanese NTSC attract movie

The static NFSPatcher hook-surface audit passed 37 guards and 5 vtables for
both variants.

## Acceptance boundary

The automated release gate proves build, package, install, rollback, startup,
and resource access. It does not claim visual free-roam acceptance, audible
Japanese police-radio acceptance, or graceful game shutdown; the test
processes were stopped after evidence collection.
