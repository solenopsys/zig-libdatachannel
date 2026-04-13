# libdatachannel Zig Wrapper

This wrapper builds upstream `paullouisageneau/libdatachannel` as a shared library through `zig build`.

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

Build for all supported targets:

```bash
zig build -Dall=true -Doptimize=ReleaseFast
```

Single-target output:

- `zig-out/lib/libdatachannel.so`
- `zig-out/include/rtc/rtc.h`
- `zig-out/include/rtc/version.h`

`-Dall=true` output:

- target-specific `.so` files are hashed and copied to `../../artifacts/libs`
- `current.json` contains target -> hash mapping

## Notes

- `NO_EXAMPLES=ON` and `NO_TESTS=ON` are enabled for wrapper builds.
- The wrapper keeps WebSocket and media features enabled by default.
