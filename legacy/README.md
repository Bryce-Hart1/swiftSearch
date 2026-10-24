# legacy/ — V1 search code, parked

This is the V1 content-search engine. **None of it compiles** against the current
`src/`, and no CMake target references it. It is kept for reference while stage 4
(search over the metadata index) is designed.

## Why it was parked

The V2 rewrite gutted `classes.hpp` and `constants.hpp` but left `parse.hpp` and
`tokenize.cpp` referencing the deleted API. The build had been broken since that
reorganization — `CMakeLists.txt` still named `src/classes.cpp`, `src/parse.cpp`
and `src/main.cpp`, none of which existed, so CMake could not even configure.
Rather than resurrect code that was being deliberately deleted, the search path
was moved here so the storage engine could move forward on a green build.

## What is here

| File | Was |
|---|---|
| `parse.hpp` | the engine workers + `assignOperation` dispatch (formerly `src/parse.cpp`) |
| `tokenize.cpp`, `tokenize.hpp` | the `#operation` / `-flag` string tables |
| `classes_v1.hpp` | formerly `src/classes.hpp`: the V1 search types *and* the superseded storage types |
| `tests/` | the four test files written against the V1 API |

`classes_v1.hpp` holds two unrelated groups. The V1 search types are
`atomCharPair`, `CharacterBucket`, `SimpleCount`. The superseded storage types are
`ImprovedBuffer`, `Envelope`, `Box`, `EnvelopeUnseal`, `HotSpot`, `LookUpTree` —
all replaced by `src/record.hpp`.

## Do not fix this in place

See `docs/revival-checklist.md`. The short version: V1 searches *file contents by
streaming bytes*; stage 4 searches *metadata already in RAM*. Different problem,
different data structure, almost no shared code. The revival is a rewrite.

Known defects, recorded so they are not rediscovered as surprises:

- `CharacterBucket::_buckets` is a never-sized `std::vector`, so the first
  `addTo()` is out of bounds.
- `SimpleCount`'s members are all private — the class is unusable as written.
- `HotSpot::EnvQuery::makeEnvQuery` has no `return` on any path.
- `Box` declares `using box_or_envelope = std::variant<Box, Envelope>` while `Box`
  is still incomplete.
- `ImprovedBuffer::makeIntoChar` is **bit-reversed**: `BitBuffer` is MSB-first
  (`bitOffset(i) = 7 - (i % 8)`, so bit 0 of a byte is `0x80`), but it writes into
  `std::bitset<8>` position 0, which is the LSB. Its only caller was
  `EnvelopeUnseal::getAsString`, which therefore could never have returned a
  correct name even with its loop condition fixed.
- `parse.hpp` has no `#pragma once` and defines non-`inline` free functions with
  bodies at namespace scope. Including it from two translation units is a
  duplicate-symbol link error.
