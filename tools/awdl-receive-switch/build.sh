#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
clang -fobjc-arc -framework Foundation -framework IOKit native-search-probe.m -o build/native-search-probe
clang native-service-browse.c -o build/native-service-browse
sdk=$(xcrun --sdk macosx --show-sdk-path)
clang -fobjc-arc -framework Foundation -F "$sdk/System/Library/PrivateFrameworks" -framework Sharing airdrop-mode-probe.m -o build/native-airdrop-mode
codesign --force --sign - --entitlements airdrop-mode-probe.entitlements build/native-airdrop-mode
./build/native-search-probe --self-test
./build/native-service-browse --self-test
# Rebuilding produces different hashes; do not mix these with a frozen beta hashes.json.
