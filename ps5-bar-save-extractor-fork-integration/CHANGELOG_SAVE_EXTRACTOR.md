# Save Extractor changelog

## 0.1.0 - experimental

- Added `ps5-bar-save-extractor.elf` as a separate target without removing upstream payloads.
- Automatic PS4 and PS5 savedata path discovery for all users.
- Recursive parent-directory creation before output writes.
- `SAVES_DUMP/manifest.tsv` with status/platform/user/title/segment/size/source path.
- Dedicated `ps5-bar-save-extractor.log`.
- Keeps upstream `ps5-bar-tool_*` build targets intact.
