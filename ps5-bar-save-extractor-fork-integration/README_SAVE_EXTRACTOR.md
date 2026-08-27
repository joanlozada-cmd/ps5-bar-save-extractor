# PS5 BAR Save Extractor (fork feature)

This fork extends **c0w-ar/ps5-bar-tool** with a focused payload that extracts save-related files only from an official PS5 Backup & Restore BAR archive.

## Attribution

- **c0w-ar** — original `ps5-bar-tool`, BAR parsing/decryption research and implementation.
- **john-tornblom / ps5-payload-dev** — PS5 Payload SDK.
- Save-extractor fork work — automatic PS4/PS5 savedata discovery, recursive output-directory creation, per-file diagnostics and manifest generation.

The upstream project is GPL-3.0 licensed; this derivative remains under the repository's GPL-3.0 license.

## New payload

Build output:

```text
bin/ps5-bar-save-extractor.elf
```

It automatically matches save-related paths for all users, including:

- `/system_data/savedata/...`
- `/system_data/savedata_prospero/...`
- `/user/home/<user>/savedata/...`
- `/user/home/<user>/savedata_meta/...`
- `/user/home/<user>/savedata_prospero/...`
- `/user/home/<user>/savedata_prospero_meta/...`
- `/user/home/<user>/savedata_prospero_for_cloud/...`
- `/user/savedata/...`

It does not intentionally dump installed games, packages, captures, trophies, licenses, or unrelated user files.

## Input

```text
USB:/PS5/EXPORT/BACKUP/archive.dat
USB:/PS5/EXPORT/BACKUP/archive0001.dat  (if present)
...
```

## Output

```text
USB:/PS5/EXPORT/BACKUP/
├── SAVES_DUMP/
│   ├── manifest.tsv
│   ├── system_data/...
│   └── user/home/.../savedata...
└── ps5-bar-save-extractor.log
```

Original BAR paths are preserved under `SAVES_DUMP` so users and title IDs do not collide.

## Current limitations

- Experimental/WIP.
- Special/multipart BAR entries are logged and skipped for now.
- Individual files larger than 2 GiB are skipped in this implementation.
- A PS5 capable of running payloads is required because `/dev/bar` is used for decryption.
- Extracted `sdimg_*` containers may still require a savedata/PFS mounting tool to access game-level files.

Always preserve the original backup files before testing.
