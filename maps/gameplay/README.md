# Authored gameplay maps

The production runtime discovers DMAP files recursively under this directory. The
filename is only a resource name; the authoritative `MapId` is the ID stored inside
the DMAP metadata.

Discovery is deterministic: candidates are sorted by path, each DMAP is decoded, and
duplicate internal IDs or broken cross-map links reject startup with a diagnostic.
Maps may live in subdirectories, but only files with the `.dmap` extension participate.

Startup policy without `--map` is:

- one discovered DMAP: use it automatically;
- multiple DMAPs: use the only map containing the `entry.start` player spawn;
- no unique `entry.start`: fail and ask for `--map <path>`;
- no DMAPs: fail with `no gameplay maps found`.

The explicit `game.exe --map <path>` override remains available. The selected map is
still registered by its internal `MapId`, and the complete discovered catalog is used
to validate links and support transitions. A map with no links is valid.

The current directory is intentionally content-owned: maps can be added, removed, or
renamed during production without changing C++ or restoring a hardcoded map manifest.
