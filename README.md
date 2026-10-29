# swiftSearch - V2.0
```
 ____          _  __ _   ____                      _     
/ ___|_      _(_)/ _| |_/ ___|  ___  __ _ _ __ ___| |__  
\___ \ \ /\ / / | |_| __\___ \ / _ \/ _` | '__/ __| '_ \ 
 ___) \ V  V /| |  _| |_ ___) |  __/ (_| | | | (__| | | |
|____/ \_/\_/ |_|_|  \__|____/ \___|\__,_|_|  \___|_| |_|
```

swiftSearch is a crash-safe key/value store for files (photos, videos, text, raw
bytes) that you talk to over HTTP. Every write is flushed to disk before the
server answers, so a value it has acknowledged survives a crash or a power cut.

This file covers building it, starting it, and what the startup options mean.
**For the HTTP routes a client can use, see [USE.md](USE.md).**

---

## How it works, briefly

- **Segment files.** Every write and every delete is appended to a segment file in
  the store folder and flushed with `fsync` (`F_FULLFSYNC` on macOS) before the
  server replies. Nothing already written is ever modified in place.
- **Index in memory.** On startup the segments are read back to rebuild the index
  of which key lives where. A write cut short by a crash is trimmed off.
- **Cache.** Recently read values are kept in RAM, up to the memory cap. A client
  can also ask for a value to be loaded ahead of time (`POST /v1/prepare/<key>`).
- **Compaction.** Overwritten and deleted values leave dead bytes behind. After the
  store has been idle for a while, a background thread rewrites the segments that
  are mostly dead and deletes the old files.

---

## Requirements

- A C++23 compiler with `<print>` and `<expected>`: GCC 14 or newer, or a recent
  Clang. Tested with GCC 16 and Apple clang 21.
- CMake 3.25 or newer.
- Linux or macOS. The CPU busy thresholds (`--cpu-high`, `--cpu-low`) only act on
  Linux; everything else works on both.

## Getting the code

The thread-safe data structures live in a git submodule at `src/bstd`, so clone
with submodules:

```bash
git clone --recurse-submodules <repo-url>
```

In a clone that already exists:

```bash
git submodule update --init
```

## Building

```bash
cmake -B build
cmake --build build -j
```

The default build type is `RelWithDebInfo`. The server ends up at
`build/bin/swiftSearch`. If you have the (local only) `tests/` folder, the build
also produces `build/bin/swiftSearch_tests`.

---

## Running

```bash
./build/bin/swiftSearch serve
```

That opens (or creates) a store in `./swiftsearch_data` and listens on
`http://127.0.0.1:8080`. The banner lists the routes, the store folder and the
limits it started with. Stop it with Ctrl-C (or `SIGTERM`): requests already in
progress are finished before it exits.

Always give the `serve` command. Running `swiftSearch` with no command prints the
logo and then waits forever; `--help` prints the options.

A typical start with a few options:

```bash
./build/bin/swiftSearch serve --store /var/lib/swiftsearch --port 9000 --mem-cap 512M --compact-idle 30m
```

### Options

| Option | Default | Meaning |
|---|---|---|
| `--store DIR` | `swiftsearch_data` | Folder the store lives in. Created if missing. |
| `--bind ADDR` | `127.0.0.1` | Address to listen on. See [Security](#security) before changing it. |
| `-p`, `--port N` | `8080` | Port to listen on. `0` picks a free one and prints it. |
| `-j`, `--threads N` | CPU count | Threads handling HTTP requests. |
| `--mem-cap SIZE` | `64M` | Bytes of cached values kept in RAM. |
| `--move-percent N` | `25` | Largest share of the memory cap one prepare may load, `1`–`100`. |
| `--cpu-threads N` | `0` | Worker threads the store reads with. `0` means one per hardware thread. |
| `--cpu-high P` | `85` | CPU busy percent at which the store halves its read parallelism. |
| `--cpu-low P` | `60` | CPU busy percent below which it adds a thread back. Must be under `--cpu-high`. |
| `--compact-idle T` | `10m` | How long the store must go without reads or writes before compaction runs. `0` turns it off. |
| `--compact-dead-percent N` | `50` | How much of a segment must be dead before compaction rewrites it, `1`–`100`. |
| `-h`, `--help` | | Print the options. |

`SIZE` is plain bytes, or a number with a `K`, `M` or `G` suffix (powers of 1024):
`512K`, `64M`, `2G`. `T` is plain seconds, or a number with an `s`, `m` or `h`
suffix: `90s`, `10m`, `1h`.

### What the limits mean

**Memory cap (`--mem-cap`).** Bounds the cache only. Every value is on disk whatever
the cap; the cap decides how many of them also sit in RAM for fast reads. When the
cache is full, the oldest cached values are dropped first. `0` turns the cache off.

**Move percent (`--move-percent`).** One `POST /v1/prepare/<key>` may load at most
this share of the memory cap. A larger value is refused with `413`, so one request
cannot push everything else out of the cache.

**CPU (`--cpu-threads`, `--cpu-high`, `--cpu-low`).** The store reads on a pool of
worker threads. `--cpu-threads` caps its size on every platform. On Linux the pool
also watches CPU usage: at `--cpu-high` percent busy it halves how many threads may
run at once, and below `--cpu-low` it adds one back.

**Compaction (`--compact-idle`, `--compact-dead-percent`).** Overwriting or deleting
a value leaves its old bytes on disk. The server counts those dead bytes per
segment file. Compaction runs only when the store has done no reads or writes for
`--compact-idle` **and** something has died. It then rewrites each segment that is
at least `--compact-dead-percent` dead: live values are copied forward and the old
file is deleted. If a request arrives mid-run, compaction stops after the batch it
is on and picks up again at the next idle stretch. It is safe to crash at any point
during compaction.

### Changing limits while it runs

All of the limits above except `--store`, `--bind`, `--port` and `--threads` can be
read and changed on a running server through `GET`/`PUT /v1/admin/limits`, for
example:

```bash
curl -X PUT 'http://127.0.0.1:8080/v1/admin/limits?mem=128M&compact_idle=5m'
```

That route only answers when the server is bound to loopback. See the admin section
of [USE.md](USE.md#admin-v1adminlimits) for every field and parameter.

---

## Files it writes

| Path | What it is |
|---|---|
| `<store>/segment-NNNNNN.dat` | The data. Records are appended; compaction replaces old files with new ones. |
| `<store>/store.meta` | When the store was created. Record timestamps count from it, so don't delete it. |
| `./swiftSearchLogs` | The log, in the folder you started the server from: replay warnings, limit changes, compaction runs, errors. |

To back a store up, stop the server and copy the whole store folder.

---

## Security

There is **no authentication**. Anyone who can reach the port can read, overwrite
and delete everything stored.

- The default `--bind 127.0.0.1` means only programs on the same machine can
  connect. `--bind 0.0.0.0` exposes every stored file to the whole network.
- The admin route is switched off whenever the bind address is not loopback, so
  the limits cannot be changed remotely.

---

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Stopped cleanly |
| `1` | Bad command or option |
| `2` | The store folder could not be opened or read |
| `3` | The server could not start, e.g. the port is already in use |

---

## License

MIT. See [LICENSE](LICENSE).
