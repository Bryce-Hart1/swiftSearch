# from_swiftsearch_handoff

**For:** the agent working on castle+.
**Goal:** run swiftSearch as a supervised backend behind castle+, so that castle+
provides everything swiftSearch deliberately does not: TLS, authentication, and
the public edge.

swiftSearch's own docs, in its repo: `README.md` (build, flags) and `USE.md`
(every HTTP route, header and status code). This file covers only what castle+
has to do.

---

## What swiftSearch is

- A crash-safe key/value store for files, spoken to over plain HTTP/1.1.
- **It has no authentication and no TLS, by design.** Anyone who can send it a
  request can read, overwrite and delete everything. castle+ is the security
  boundary.
- It binds `127.0.0.1` by default. Every request castle+ forwards arrives from
  loopback, so swiftSearch cannot tell an outside client from a local one.
- Routes (all under the backend's root, none need rewriting):

| Method | Path | Purpose |
|---|---|---|
| `PUT` | `/v1/blobs/<key>` | store a value (body = value, `Content-Length` required, max 64 MiB) |
| `GET` `HEAD` | `/v1/blobs/<key>` | read a value; supports `Range`, `If-None-Match`, `If-Range` |
| `DELETE` | `/v1/blobs/<key>` | delete a value |
| `POST` | `/v1/prepare/<key>` | load a value into memory |
| `GET` `HEAD` | `/healthz` | liveness, `{"status":"ok"}` |
| `GET` `PUT` | `/v1/boxes/<key>` | reserved, always `501` today |
| `GET` `PUT` | `/v1/admin/limits` | admin. **Off unless started with `--admin`. Never enable it behind castle+.** |

---

## Must do (security)

1. **Authenticate every request on the swiftSearch route before forwarding it.**
   castle+'s README lists no auth feature today, so this likely needs building.
   - Reject unauthenticated requests at castle+ with `401`; never let one reach
     the backend.
   - A bearer token (or Basic auth) over TLS is enough to start. Compare tokens in
     constant time.
   - Strip the credential header (`Authorization`) before forwarding.
     swiftSearch ignores it, and it shouldn't end up anywhere downstream.
   - Nice to have: a read-only credential allowed only `GET`/`HEAD`, and a
     read-write one allowed `PUT`/`DELETE`/`POST` as well.
2. **Never forward `/v1/admin/`.** Answer `404` for that prefix at castle+. It is
   already off in swiftSearch unless `--admin` is passed, but this is the second lock:
   if someone adds the flag by mistake, the internet still can't reach it.
3. **Don't start swiftSearch with `--admin` or `--bind 0.0.0.0`.** Keep it on
   `127.0.0.1` so castle+ is the only way in.
4. **Wait for the old process to be reaped before restarting.** swiftSearch locks
   its store folder (`flock` on `store.lock`). A second copy on the same `--store`
   exits at once with code `2` and "is already open in another swiftSearch
   process" instead of corrupting anything. A restart that races a slow shutdown
   would therefore fail and count toward quarantine, rather than doing damage.

## Must do (correctness)

5. **Route by host to the backend root, without stripping any prefix.**
   swiftSearch expects paths exactly as in the table above. For example:

   ```ini
   [vault]
   host    = vault.example.com
   path    = /
   backend = 127.0.0.1:9101
   ```

6. **Cap bodies at 64 MiB on this route.** That's swiftSearch's limit, so anything
   larger gets `413` from it anyway. Rejecting at castle+ saves streaming up to
   1 GiB (castle+'s default `--max-body`) just to be refused. If `--max-body` is
   global only, consider a per-route value.
7. **Handle `Expect: 100-continue`.** curl sends it for any upload over 1 KB.
   swiftSearch answers it with an interim `HTTP/1.1 100 Continue`, followed later
   by the real response. castle+ must either:
   - strip `Expect` before forwarding, and answer the client's `100 Continue`
     itself (simplest), or
   - forward it, and relay the interim `100` to the client without treating it
     as the final response.

   Test this one explicitly: `curl -T` with a file of a few MB.
8. **Pass these headers through untouched,** in both directions:
   - **Requests:** `Range`, `If-None-Match`, `If-Range`, `Content-Type`,
     `Content-Length`, `X-SwiftSearch-Type`.
   - **Responses:** `ETag`, `Accept-Ranges`, `Content-Range`, `Cache-Control`,
     `Content-Type`, `X-SwiftSearch-Type`, `X-SwiftSearch-Hash`, `Location`,
     `X-Content-Type-Options`.

   Pass `206`, `304` and `416` through as-is. A `304` has no body. Video seeking
   depends on all of this.
9. **Chunked uploads:** castle+ already answers `501`, which matches swiftSearch,
   since it requires `Content-Length` too. Nothing to change, just don't convert
   bodies to chunked when forwarding.

---

## Supervision (services.conf)

```ini
[vault]
exec        = /opt/swiftsearch/swiftSearch
args        = serve --store /var/lib/swiftsearch --bind 127.0.0.1 --port 9101 -j 2 --mem-cap 256M
workdir     = /var/lib/swiftsearch-logs
autorestart = true
health_tcp  = 127.0.0.1:9101
```

- **Absolute `--store`.** Set a working dir too, because swiftSearch writes its
  log file `swiftSearchLogs` into whatever directory it starts in. Use whatever
  castle+ calls its working-directory option.
- **`-j` sets memory as well as threads.** Each request holds its whole body (up
  to 64 MiB) in memory, and up to 4 requests per thread can wait in the queue. So
  the worst case is about `64 MiB × 5 × j`, which is about 640 MiB at `-j 2`. Size
  it to the box's RAM.
- **Shutdown:** `SIGTERM` or `SIGINT` makes it finish in-flight requests, with a
  5 s grace period. `SIGKILL` loses nothing that was acknowledged, because every
  write is flushed to disk before it's answered. Still, prefer `SIGTERM` and then
  wait.
- **Exit codes:**

| Code | Meaning | Restart helps? |
|---|---|---|
| `0` | clean stop | — |
| `1` | bad flag | No: fix the config |
| `2` | store folder can't be opened, or another copy has it open | No: check path/permissions, or the old process is still running |
| `3` | couldn't start, e.g. port in use | Maybe |

  Quarantining on repeated `1` or `2` is the right behavior.
- **Health:** `health_tcp` works. `GET /healthz` is a stronger check if castle+ can
  do HTTP probes. `/healthz` is fine to leave public; it reveals nothing.

---

## Build on the target box

- **Toolchain:** C++23 with `<print>`, which needs GCC 14 or newer: Alpine 3.21+,
  not 3.20. Also CMake 3.25 or newer.
- **Get the code:**
  ```sh
  git clone --recurse-submodules <swiftsearch repo>
  ```
  `src/bstd` is a submodule.
- **Build:**
  ```sh
  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ```
  The binary is `build/bin/swiftSearch`.
- **Never yet built on Linux or musl.** It was developed on macOS. Expect to
  fix small portability issues on the first build, and run its test binary on the
  box if the `tests/` folder is available.

---

## Acceptance checks, run through castle+ over HTTPS

```sh
H=https://vault.example.com; A='Authorization: Bearer <token>'
curl -s -o /dev/null -w '%{http_code}\n' $H/v1/blobs/x                        # 401: no credential
curl -s -H "$A" -T photo.jpg $H/v1/blobs/photos/photo.jpg                     # 201 + JSON (exercises Expect)
curl -s -H "$A" -o out.jpg $H/v1/blobs/photos/photo.jpg && cmp photo.jpg out.jpg
curl -s -H "$A" -H 'Range: bytes=0-99' -D - -o /dev/null $H/v1/blobs/photos/photo.jpg   # 206, Content-Range
curl -s -H "$A" -o /dev/null -w '%{http_code}\n' $H/v1/admin/limits          # 404, even with a credential
curl -s -H "$A" -X DELETE -o /dev/null -w '%{http_code}\n' $H/v1/blobs/photos/photo.jpg  # 204
head -c 70000000 /dev/zero > big.bin
curl -s -H "$A" -T big.bin -o /dev/null -w '%{http_code}\n' $H/v1/blobs/big  # 413, refused at the edge
```

Also: kill the swiftSearch process and check that castle+ restarts it, and that
the photo uploaded before the kill still reads back.
