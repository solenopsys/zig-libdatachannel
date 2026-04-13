# libdatachannel Zig Wrapper

This wrapper builds:

- upstream `paullouisageneau/libdatachannel` as shared library
- runtime C-ABI helper `libdatachannel_wrapper.so` (dlopen-based thin API layer)

## Build

Prerequisites:

- Zig `0.15.2+`
- `cmake` and `ninja`
- initialized submodules (including nested deps)

Initialize dependencies:

```bash
git submodule update --init --recursive native/wrapers/libdatachannel/vendor/libdatachannel
```

Build for current target:

```bash
zig build -Doptimize=ReleaseFast
```

Build only the C-ABI helper (skip upstream core build):

```bash
zig build -Dffi_only=true -Doptimize=ReleaseFast
```

Build for all supported targets:

```bash
zig build -Dall=true -Doptimize=ReleaseFast
```

Single-target output:

- `zig-out/lib/libdatachannel.so`
- `zig-out/lib/libdatachannel_wrapper.so`
- `zig-out/include/rtc/rtc.h`
- `zig-out/include/rtc/version.h`
- `zig-out/include/libdatachannel_wrapper.h`

`-Dall=true` output:

- target-specific `.so` files are hashed and copied to `../../artifacts/libs`
- `current.json` contains target -> hash mapping

## Notes

- `NO_EXAMPLES=ON` and `NO_TESTS=ON` are enabled for wrapper builds.
- The wrapper keeps WebSocket and media features enabled by default.

## Wrapper API Coverage

`libdatachannel_wrapper.h` exposes C-ABI helpers for:

- PeerConnection create/close/delete and SDP/candidate exchange.
- Local/remote SDP reads (including SDP type).
- Track flow (`rtcAddTrackEx` with Opus defaults + track callbacks).
- DataChannel flow (`create`, `create_ex`, `on_datachannel`, open/close/error/message callbacks).
- State callbacks (`connection`, `ice`, `gathering`, `signaling`).
- Generic ID helpers (`send`, `close`, `delete`, `is_open`, `is_closed`).
