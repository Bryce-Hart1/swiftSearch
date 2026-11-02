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
- **Access keys.** Every request except `GET /healthz` must carry an access key.
  The store keeps only a salted hash of each key, never the key itself.
- **Salted hashes.** The hash of a value that the server sends back (`ETag`,
  `X-SwiftSearch-Hash`) is keyed with a secret salt that belongs to this store.

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

Make an access key first. Without one, the server answers every request except
`/healthz` with `401`:

```bash
./build/bin/swiftSearch key add
```

The key goes to stdout (`ssk_…`, 77 characters) and is shown **only this once**.
Save it somewhere safe. Then start the server:

```bash
./build/bin/swiftSearch serve
```

That opens (or creates) a store in `./swiftsearch_data` and listens on
`http://127.0.0.1:8080`. The banner lists the routes, the store folder, how many
access keys it loaded and the limits it started with. Stop it with Ctrl-C (or
`SIGTERM`): requests already in progress are finished before it exits.

Send the key with every request:

```bash
KEY=ssk_...   # what key add printed
curl -H "Authorization: Bearer $KEY" http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

Always give a command (`serve` or `key`). Running `swiftSearch` with no command
prints the logo and then waits forever; `--help` prints the options.

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
| `--admin` | off | Serve `GET`/`PUT /v1/admin/limits` to change limits while running. Needs an `admin` key, and works on loopback binds only. See [below](#changing-limits-while-it-runs). |
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

Start with `--admin` and all of the limits above except `--store`, `--bind`,
`--port` and `--threads` can be read and changed on a running server through
`GET`/`PUT /v1/admin/limits`, using an `admin` key:

```bash
./build/bin/swiftSearch key add --access admin --name ops
./build/bin/swiftSearch serve --admin
curl -H "Authorization: Bearer $ADMIN_KEY" -X PUT 'http://127.0.0.1:8080/v1/admin/limits?mem=128M&compact_idle=5m'
```

The route is **off by default**, and stays off unless the server is bound to
loopback. `read` and `write` keys get `403` on it. Behind a reverse proxy on the
same machine (such as castle+), the admin key is the only thing protecting it,
because the proxy connects from `127.0.0.1` for every outside client. Leave
`--admin` off there unless you need it. See the admin section of
[USE.md](USE.md#admin-v1adminlimits) for every field and parameter.

---

## Access keys

Every route except `GET /healthz` needs a key, sent as
`Authorization: Bearer <key>`. No key, or one the store doesn't know, gets `401`.
A key that isn't allowed to do what it asked gets `403`.

| Access | Allows |
|---|---|
| `read` | `GET` and `HEAD` |
| `write` (the default) | also `PUT`, `DELETE` and `POST /v1/prepare` |
| `admin` | also `/v1/admin/limits`, when the server runs with `--admin` |

```bash
./build/bin/swiftSearch key add [--store DIR] [--access read|write|admin] [--name NAME]
./build/bin/swiftSearch key list [--store DIR]
./build/bin/swiftSearch key remove ID [--store DIR]
```

- **`key add`** prints the new key on stdout and nothing else, so
  `KEY=$(swiftSearch key add)` works. The notes go to stderr. The key is never
  written anywhere, so if you lose it, remove it and make a new one.
- **`key list`** shows each key's id (the 8 hex digits after `ssk_`), its access
  and its name. It cannot show the keys themselves, because the store doesn't have them.
- **`key remove`** revokes a key.

You can run all three while the server is up. They only touch
`<store>/access.keys`, and a running server picks up the change within a second,
with no restart. If that file is damaged, the server refuses every key (and logs
why) rather than keep using the old list.

**How keys are stored.** A key is `ssk_<id>_<secret>`, where the secret is 32
random bytes. `access.keys` holds, for each key, a random 16-byte salt and
`HMAC-SHA256(salt, key)`. A copy of the store folder or a backup therefore
contains nothing that can open the store. Since keys are random rather than chosen
by people, a slow password hash would add nothing, and the check stays fast. Every
comparison runs in constant time.

**Hashes are salted too.** The hash in `ETag`, `X-SwiftSearch-Hash` and the `hash`
field of a `PUT` is `HMAC-SHA256(store salt, value)`, where the salt is 32 random
bytes made on first start and kept in `<store>/store.salt`. The same file gets an
unrelated hash in another store, and nobody can check a file they already have
against a hash they saw. Within one store the hash is stable, across restarts too,
so ETags work as before. It is **not** the `sha256sum` of the file.

---

## Files it writes

| Path | What it is |
|---|---|
| `<store>/segment-NNNNNN.dat` | The data. Records are appended; compaction replaces old files with new ones. |
| `<store>/store.meta` | When the store was created. Record timestamps count from it, so don't delete it. |
| `<store>/access.keys` | The access keys, as salted hashes only. Mode `0600`. Edited by `swiftSearch key`; deleting a line revokes that key. |
| `<store>/store.salt` | The store's secret salt for hashes. Mode `0600`. Made on first start; deleting it changes every hash (and ETag) the store hands out. |
| `<store>/store.lock` | Locked while a server has the store open, so a second server on the same folder refuses to start instead of corrupting it. The kernel releases it when the process exits, even after a crash; the file staying behind is normal. |
| `./swiftSearchLogs` | The log, in the folder you started the server from: replay warnings, limit changes, compaction runs, errors. |

To back a store up, stop the server and copy the whole store folder. The copy
includes `store.salt`, so keep backups as private as the data in them.

---

## Security

- **Every request needs a key**, except `GET /healthz`, which only says the server
  is up. Without a valid key a request gets `401` and no data: not a value, not
  whether a key exists, not even whether a route exists. An upload without a valid
  key is refused as soon as its headers arrive, before the server reads any of the
  body (`curl` never gets a `100 Continue`).
- **Keys travel as plain text over plain HTTP.** swiftSearch has no TLS. On loopback
  (the default `--bind 127.0.0.1`) that's fine. To reach it over a network, put a
  TLS proxy in front, or anyone who can see the traffic can copy a key. Don't
  expose `--bind 0.0.0.0` to the internet without one.
- **Give each client its own key, with the least access it needs.** A viewer gets a
  `read` key. Revoke one with `key remove` without touching the others.
- **Keep the admin route off** unless you need it. It needs an `admin` key and a
  loopback bind.

---

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Stopped cleanly |
| `1` | Bad command or option |
| `2` | The store folder could not be opened or read, or another swiftSearch already has it open. For `key`: `access.keys` could not be read or written |
| `3` | The server could not start, e.g. the port is already in use |

---

## License

MIT. See [LICENSE](LICENSE).
