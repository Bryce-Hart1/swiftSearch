# Using swiftSearch over HTTP

This is the reference for everything a client can ask a running swiftSearch server
to do. For building it, starting it, and what the startup flags mean, see
[README.md](README.md).

Everything here assumes the default address, `http://127.0.0.1:8080`. Change the
host and port to match the `--bind` and `--port` you started with.

> **There is no authentication.** Anyone who can reach the port can read, write
> and delete everything. The server listens on loopback (`127.0.0.1`) unless you
> tell it otherwise.

---

## Quick start

```bash
# store a file under the key notes/hello.txt
curl -X PUT --data-binary @hello.txt http://127.0.0.1:8080/v1/blobs/notes/hello.txt

# read it back
curl http://127.0.0.1:8080/v1/blobs/notes/hello.txt

# headers only
curl -I http://127.0.0.1:8080/v1/blobs/notes/hello.txt

# pull it into memory so the next read skips the disk
curl -X POST http://127.0.0.1:8080/v1/prepare/notes/hello.txt

# delete it
curl -X DELETE http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

Use `--data-binary`, not `-d`. `-d` strips newlines from the file, so you would
store something other than what is on your disk.

---

## Routes at a glance

| Method | Path | What it does | Success |
|---|---|---|---|
| `PUT` | `/v1/blobs/<key>` | Store a value; the request body is the value | `201` new, `200` replaced |
| `GET` | `/v1/blobs/<key>` | Read a value; the response body is the value | `200`, `206`, `304` |
| `HEAD` | `/v1/blobs/<key>` | The same headers as `GET`, no body | `200` |
| `DELETE` | `/v1/blobs/<key>` | Remove a value | `204` |
| `POST` | `/v1/prepare/<key>` | Load a value from disk into memory | `204` |
| `GET` `HEAD` | `/healthz` | Is the server up? | `200` |
| `GET` `PUT` | `/v1/admin/limits` | Read or change the server's limits (`--admin`, loopback only) | `200` |
| `GET` `PUT` | `/v1/boxes/<key>` | Folders of keys. **Not built yet**, always `501` | — |

Any other method on a known path gets `405` with an `Allow` header listing what
that path accepts. A path that matches nothing gets `404 no such route`.

---

## Keys

The URL path after the route prefix **is** the key. In
`/v1/blobs/photos/2026/beach.jpg` the key is `photos/2026/beach.jpg`.

A key is a path of segments separated by `/`. The rules:

- **Not empty,** with no `/` at the start or end and no empty segment (`a//b`).
- **No `.` or `..` segment.** They mean something in a URL that they don't mean in a key.
  Note that curl removes them before sending unless you pass `--path-as-is`.
- **No encoded slash (`%2F`).** A key with a slash in it is a key with another segment;
  write the slash.
- **No control characters** (bytes below `0x20`, and `0x7F`), even percent-encoded.
- **Valid UTF-8** after percent-decoding.
- **At most 64 KiB.** In practice the 8 KiB limit on the whole request head is reached
  first, and that is answered with `431`.

Anything else can be percent-encoded: a space is `%20`. A `+` is a literal plus sign,
not a space.

A key that breaks a rule gets `400` with the reason in the error body, for example
`{"error":"encoded separator (%2F) in key",...}`.

The extension at the end of the key (`.jpg`, `.mp4`, `.txt`) matters: it decides
the `Content-Type` the value is served with. See [Value types](#value-types).

---

## Blobs

### `PUT /v1/blobs/<key>`: store a value

The request body is the value, byte for byte. A `PUT` to a key that already
exists replaces the whole value.

```bash
curl -X PUT --data-binary @beach.jpg http://127.0.0.1:8080/v1/blobs/photos/beach.jpg
```

**Request**

| Header | |
|---|---|
| `Content-Length` | **Required.** Without it the answer is `411` and the connection closes. Chunked uploads (`Transfer-Encoding`) are not supported: `501`. |
| `X-SwiftSearch-Type` | Optional. What kind of value this is: one of the [type names](#value-types). An unknown name is refused with `400` rather than guessed at. |
| `Content-Type` | Optional. Only used to guess the type when `X-SwiftSearch-Type` is absent. |

Without `X-SwiftSearch-Type`, the type is worked out in this order:
1. **From `Content-Type`:**
   - `image/*` is `img`;
   - `video/*` is `video`;
   - `text/*` or `application/json` is `str`.
2. **From the key's extension:** a known image, video or text extension.
3. **Otherwise:** `bin`.

curl sends `Content-Type: application/x-www-form-urlencoded` by default, which says
nothing, so with plain curl it is the extension that decides.

The largest value is **64 MiB**. A bigger body gets `413`. If the client sent
`Expect: 100-continue` (curl does for anything over 1 KB), that happens before the
body is uploaded at all.

**Response:** `201 Created` for a new key, with `Location: /v1/blobs/<key>`. `200 OK`
when an existing value was replaced. Both carry `X-SwiftSearch-Hash` and a JSON body:

```json
{"key":"notes/hello.txt","hash":"c0757014a9…7a56e49","size":19,"type":"str","created":true,"deduped":false}
```

| Field | |
|---|---|
| `key` | The key as stored, percent-decoded |
| `hash` | SHA-256 of the value, in hex |
| `size` | Bytes stored |
| `type` | The type it was stored as |
| `created` | `true` for a new key, `false` for a replacement |
| `deduped` | Always `false` for now; deduplication is not built yet |

**Durable on success.** By the time a `200` or `201` arrives, the value has been
written and flushed to disk (`fsync`, or `F_FULLFSYNC` on macOS). A crash after that
loses nothing.

Storing the same bytes again still writes a new copy, and the old one becomes dead
space until compaction reclaims it.

### `GET /v1/blobs/<key>`: read a value

```bash
curl http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

The body is the value exactly as stored. The headers:

| Header | |
|---|---|
| `Content-Type` | From the stored type and the key's extension. See [Value types](#value-types). |
| `X-SwiftSearch-Type` | The stored type name, e.g. `str`, `img` |
| `X-SwiftSearch-Hash` | SHA-256 of the whole value, in hex |
| `ETag` | The same hash in quotes. Use it for conditional requests. |
| `Accept-Ranges` | `bytes`: ranges are supported |
| `Cache-Control` | `no-cache`: a cache may keep it but must check back, because a `PUT` can change what a key holds |
| `Content-Length` | Size of the body being sent |

**Errors:**
- **`404`** when the key does not exist.
- **`500`** when the key exists but its stored record fails its checksum. A damaged
  value is never sent. The server logs it, and the key still exists.

#### Conditional reads: `If-None-Match`

Send back the `ETag` you got earlier. If the value has not changed you get
`304 Not Modified` with no body:

```bash
curl -H 'If-None-Match: "c0757014a9…7a56e49"' http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

`*` matches any existing value. A list of tags and weak tags (`W/"…"`) are accepted.

#### Partial reads: `Range`

One byte range per request, which is what video players send:

```bash
curl -H "Range: bytes=0-4" http://127.0.0.1:8080/v1/blobs/notes/hello.txt
# 206 Partial Content, Content-Range: bytes 0-4/19, body "hello"
```

| You send | You get |
|---|---|
| A satisfiable range: `bytes=0-99`, `bytes=100-`, `bytes=-100` (the last 100) | `206` with `Content-Range: bytes first-last/size` |
| A range that starts past the end, or any range of an empty value | `416` with `Content-Range: bytes */size`, so you know the real size |
| A malformed range, another unit, or more than one range | The range is ignored and the whole value comes back as `200` |

`If-Range` is supported with an ETag: the range is honoured only if the tag still
matches exactly. Otherwise the whole, current value comes back as `200`, so a resumed
download never gets bytes of a different version spliced on. Weak tags and dates never
match.

`If-None-Match` is checked before `Range`. A match is a `304` even if a range was asked for.

Today a range is cut from the full value after it is read, so seeking in a large video
still reads the whole thing from disk.

### `HEAD /v1/blobs/<key>`

Exactly the headers `GET` would send, `Content-Length` included, with no body. Ranges
and conditionals work the same way. Use it to check whether a key exists, or to read its
size, type or hash:

```bash
curl -I http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

### `DELETE /v1/blobs/<key>`: remove a value

```bash
curl -X DELETE http://127.0.0.1:8080/v1/blobs/notes/hello.txt
```

`204 No Content` when it was deleted, `404` when there was no such key. Like a `PUT`,
a `204` means the delete is on disk and survives a crash. The space the value used
is reclaimed later by compaction (see README).

---

## Value types

The type is stored with the value and comes back in `X-SwiftSearch-Type`. Names are
case-insensitive when you send them.

| Name | Meaning | Served as |
|---|---|---|
| `img` | An image | By extension: `.jpg` `.jpeg` `image/jpeg`, `.png` `image/png`, `.gif` `image/gif`, `.webp` `image/webp`, `.avif` `image/avif`, `.bmp` `image/bmp`, `.tif` `.tiff` `image/tiff`, `.heic` `image/heic`, `.heif` `image/heif`. Anything else: `application/octet-stream` |
| `video` | A video | By extension: `.mp4` `video/mp4`, `.m4v` `video/x-m4v`, `.webm` `video/webm`, `.mov` `video/quicktime`, `.mkv` `video/x-matroska`, `.ogv` `video/ogg`, `.avi` `video/x-msvideo`, `.ts` `video/mp2t`. Anything else: `application/octet-stream` |
| `str` | Text | By extension: `.md` `text/markdown`, `.csv` `text/csv`, `.json` `application/json`. Anything else, including `.txt` and `.log`: `text/plain; charset=utf-8` |
| `bin` | Raw bytes of no stated kind (the default) | `application/octet-stream` |
| `char` `int` `big_int` `float` `big_float` | A scalar, stored as its raw little-endian bytes (1, 4, 8, 4 and 8 bytes) | `application/octet-stream` |

**HTML and SVG are never served as such.** An `.html` file stored as `str` comes back as
`text/plain`, and an `.svg` stored as `img` comes back as `application/octet-stream`.
Both formats can run script in a browser, and the API and stored files share an origin.
Every response also carries `X-Content-Type-Options: nosniff`, so the browser does not
guess a type of its own.

---

## `POST /v1/prepare/<key>`: load into memory

Reads the value from disk into the server's in-memory cache, so the next `GET` is
served from RAM. No request body is needed.

```bash
curl -X POST http://127.0.0.1:8080/v1/prepare/videos/intro.mp4
```

| Status | When |
|---|---|
| `204` | The value is in memory, including when it already was |
| `404` | No such key |
| `413` | The value is bigger than one prepare may load, which is the move percent of the memory cap (25% of 64 MiB by default; see `move_max` in the admin route). Nothing is read from disk. |

The value is copied, not moved: it stays on disk. The cached copy is **not pinned**.
It counts against the memory cap like any other cached value and can be pushed out
again by later reads, oldest first.

---

## `GET /healthz`

```bash
curl http://127.0.0.1:8080/healthz
# {"status":"ok"}
```

`200` whenever the server is accepting requests. `HEAD` works too. It does not touch
the store, so it does not keep compaction from running.

---

## Admin: `/v1/admin/limits`

Read and change the server's limits while it runs. This route **only exists when the
server was started with `--admin` and is bound to a loopback address** (`127.x.x.x`,
`::1` or `localhost`). Otherwise it answers `404 no such route`, and the limits can
only be set by the startup flags. Other methods get `405` with `Allow: GET, PUT`.

It is off by default because loopback is not proof that a request is local: a
reverse proxy on the same machine forwards every outside client from `127.0.0.1`.
Don't turn it on behind one.

### `GET /v1/admin/limits`

```bash
curl http://127.0.0.1:8080/v1/admin/limits
```

```json
{"mem_cap":67108864,"mem_used":27,"move_percent":25,"move_max":16777216,"cpu_threads":8,
 "cpu_high":85,"cpu_low":60,"request_threads":8,"keys":2,"dead_bytes":66,"disk_bytes":188,
 "segments":1,"compact_idle":600,"compact_dead_percent":50}
```

| Field | |
|---|---|
| `mem_cap` | Bytes of cached values the server may hold in RAM |
| `mem_used` | Bytes of cached values it holds now |
| `move_percent` | Share of `mem_cap` one prepare may load |
| `move_max` | That share in bytes: the largest value a prepare accepts |
| `cpu_threads` | Worker threads the store uses for reads |
| `cpu_high` `cpu_low` | Busy percentages the store backs off at and ramps up below (only act on Linux) |
| `request_threads` | Threads handling HTTP requests. Fixed at startup. |
| `keys` | Number of keys stored |
| `dead_bytes` | Bytes on disk held by overwritten and deleted values: what compaction can win back |
| `disk_bytes` | Bytes in all segment files, live and dead |
| `segments` | Number of segment files |
| `compact_idle` | Seconds of no reads or writes before compaction runs; `0` means it is off |
| `compact_dead_percent` | How much of a segment must be dead before compaction rewrites it |

### `PUT /v1/admin/limits?…`

The changes go in the query string. Give only what you want to change; the rest
keeps its current value. The answer is the same JSON as `GET`, showing the new values.

```bash
curl -X PUT 'http://127.0.0.1:8080/v1/admin/limits?mem=128M&compact_idle=5m'
```

| Parameter | Accepts |
|---|---|
| `mem` | Bytes, or with a `K`, `M` or `G` suffix (powers of 1024): `512K`, `128M`, `2G`. Shrinking it drops the oldest cached values at once; they stay on disk. `0` turns the cache off. |
| `move_percent` | `1` to `100` |
| `cpu_threads` | `0` to `1024`. `0` means one per hardware thread. |
| `cpu_high` `cpu_low` | Percentages with `cpu_low` < `cpu_high` ≤ `100` |
| `compact_idle` | Seconds, or with an `s`, `m` or `h` suffix: `90s`, `10m`, `1h`. `0` turns automatic compaction off. At most 30 days. |
| `compact_dead_percent` | `1` to `100` |

**All or nothing.** Every parameter is checked before any is applied, so a request
with one bad value changes nothing. The `400` says which value was wrong. A
misspelled parameter name (`?memory=1`) is also a `400`, rather than a change that
silently does nothing. A `PUT` with no parameters at all is a `400` too.

---

## Boxes: not built yet

`GET /v1/boxes/<key>` (list the keys under a prefix) and `PUT /v1/boxes/<key>`
(folder metadata) are reserved. Both answer `501 Not Implemented` until the store
can scan by prefix.

---

## Errors

Every error has a JSON body of the same shape:

```json
{"error":"no such key","code":"not_found","status":404}
```

`error` is a sentence for people; `code` is stable and meant for programs to check.

| Status | `code` | Common causes |
|---|---|---|
| `400` | `bad_request` | A key that breaks a [key rule](#keys), an unknown `X-SwiftSearch-Type`, a bad admin parameter, a malformed request |
| `404` | `not_found` | No such key; no such route; the admin route without `--admin` or on a non-loopback bind |
| `405` | `method_not_allowed` | A method that path does not take. The `Allow` header lists the ones it does. |
| `408` | `request_timeout` | The request arrived too slowly (see [Limits](#limits-and-timeouts)) |
| `411` | `length_required` | `PUT` without `Content-Length` |
| `413` | `content_too_large` | A body over 64 MiB; a prepare over `move_max` |
| `414` | `uri_too_long` | A key over 64 KiB |
| `416` | `range_not_satisfiable` | A `Range` that starts past the end of the value |
| `431` | `headers_too_large` | Request head over 8 KiB or more than 64 headers |
| `500` | `internal_error` | A stored record failed its checksum, or the disk failed |
| `501` | `unsupported` | Box routes; `Transfer-Encoding` uploads |
| `503` | `unavailable` | Too many requests queued, or the server is shutting down. Retry. |
| `505` | `bad_version` | Anything other than HTTP/1.0 or HTTP/1.1 |

Errors that leave the connection in an unknown state also close it
(`Connection: close`): a malformed request, an unknown method, `408`, `411`, `413`
and `503`. Open a new connection and try again.

---

## Limits and timeouts

| Limit | Value |
|---|---|
| Largest value | 64 MiB |
| Longest key | 64 KiB, but the request head limit below comes first |
| Request head (request line + headers) | 8 KiB, at most 64 headers |
| Time to send the request head | 10 seconds |
| Gap allowed while sending a body | 20 seconds with no bytes |
| Whole request, start to finish | 10 minutes |
| Gap allowed while reading a response | 20 seconds without reading anything |
| Idle keep-alive connection | Closed after 15 seconds |
| Requests on one connection | 1000, then it is closed |
| Requests waiting for a worker | 4 per request thread; past that, `503` |
| HTTP versions | 1.1 and 1.0. Keep-alive is the default on 1.1. |

Every response carries `Content-Length`, a `Connection` header saying whether the
connection stays open, and `X-Content-Type-Options: nosniff`.
