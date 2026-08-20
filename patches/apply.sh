#!/usr/bin/env sh
# Applies the local patches to the vendored libdatachannel checkout.
#
# The C API has no way to send an extra HTTP header on a WebSocket handshake,
# but the C++ one does (WebSocket::open(url, headers)). Our wrapper needs it for
# `Authorization: Bearer ...` on the OpenAI Realtime socket, so we export that
# single-header case as rtcCreateWebSocketWithHeader.
#
# Idempotent: re-running is a no-op, so incremental builds stay cheap and the
# checkout can be reset with `git checkout` at any time.
set -eu

vendor="$(dirname "$0")/../vendor/libdatachannel-vendor"

if grep -q rtcCreateWebSocketWithHeader "$vendor/src/capi.cpp"; then
    exit 0
fi

patch -p1 -d "$vendor" < "$(dirname "$0")/websocket-with-header.patch"
