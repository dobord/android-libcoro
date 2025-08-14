#!/usr/bin/env bash

# OpenSSL static build script for android-libcoro project
#  - Builds only OpenSSL (no libssh) for selected Android ABIs
#  - Places results under external/openssl/<ABI>
#  - Intended for use with libcoro TLS (find_package(OpenSSL) inside subdirectory)

set -euo pipefail

OPENSSL_VERSION="${OPENSSL_VERSION:-3.5.0}"
MIN_API_LEVEL="${MIN_API_LEVEL:-24}"

info(){ echo -e "[INFO] $*"; }
err(){ echo -e "[ERR ] $*" >&2; }

usage(){ cat <<EOF
Usage: $0 [options]
    --abis "a;b;c"   Limit ABIs (default: arm64-v8a;armeabi-v7a;x86_64;x86 or ANDROID_ABI)
    --version X.Y.Z  OpenSSL version (default ${OPENSSL_VERSION})
    --api N          Android API level (default ${MIN_API_LEVEL})
    --clean          Remove build cache before building
Examples:
    ANDROID_ABI=arm64-v8a $0
    $0 --abis "arm64-v8a;x86_64" --api 26
EOF
}

ABIS_ARG=""
CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --abis) ABIS_ARG="$2"; shift 2;;
        --version) OPENSSL_VERSION="$2"; shift 2;;
        --api) MIN_API_LEVEL="$2"; shift 2;;
        --clean) CLEAN=1; shift;;
        -h|--help) usage; exit 0;;
        *) err "Unknown arg $1"; usage; exit 1;;
    esac
done

# NDK discovery
if [[ -z "${ANDROID_NDK_HOME:-}" && -z "${ANDROID_NDK_ROOT:-}" ]]; then
    if [[ -d /opt/android-sdk/ndk ]]; then
        export ANDROID_NDK_HOME="/opt/android-sdk/ndk/$(ls /opt/android-sdk/ndk | sort -V | tail -1)"
    elif [[ -d $HOME/Android/Sdk/ndk ]]; then
        export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk | sort -V | tail -1)"
    else
        err "Android NDK not found; set ANDROID_NDK_HOME"; exit 1
    fi
fi
NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
info "Using NDK: $NDK_PATH"

command -v perl >/dev/null || { err "perl required (sudo apt-get install perl)"; exit 1; }
command -v curl >/dev/null || { err "curl required"; exit 1; }

if [[ -n "${ANDROID_ABI:-}" && -z "$ABIS_ARG" ]]; then
    ABIS=("${ANDROID_ABI}")
elif [[ -n "$ABIS_ARG" ]]; then
    IFS=';' read -r -a ABIS <<<"$ABIS_ARG"
else
    ABIS=(arm64-v8a armeabi-v7a x86_64 x86)
fi
info "Target ABIs: ${ABIS[*]}"

ROOT_DIR="$(pwd)"
CACHE_DIR="$ROOT_DIR/.openssl_build"
SRC_DIR="$CACHE_DIR/src"
BUILD_ROOT="$CACHE_DIR/build"
INSTALL_ROOT="$ROOT_DIR/external/openssl"
TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"

[[ $CLEAN -eq 1 ]] && { info "Cleaning cache"; rm -rf "$CACHE_DIR"; }
mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$INSTALL_ROOT"

download() {
    local out="$CACHE_DIR/$TARBALL"
    if [[ -f $out ]]; then
        info "Tarball cached: $out"; return 0; fi
    local urls=(
        "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://ftp.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://mirror.yandex.ru/pub/OpenSSL/openssl-${OPENSSL_VERSION}.tar.gz"
    )
    info "Downloading OpenSSL ${OPENSSL_VERSION}";
    for u in "${urls[@]}"; do
        info " -> $u"
        if curl -fsSL "$u" -o "$out.tmp"; then
            mv "$out.tmp" "$out"; info "Downloaded"; return 0; fi
    done
    err "Failed to download OpenSSL tarball"; return 1
}

extract() {
    local dst="$SRC_DIR/openssl-${OPENSSL_VERSION}"
    [[ -d $dst ]] && { info "Sources already extracted"; return 0; }
    tar -xf "$CACHE_DIR/$TARBALL" -C "$SRC_DIR"
}

abi_target() {
    case "$1" in
        arm64-v8a) echo android-arm64;;
        armeabi-v7a) echo android-arm;;
        x86_64) echo android-x86_64;;
        x86) echo android-x86;;
        *) err "Unsupported ABI $1"; return 1;;
    esac
}

build_one() {
    local abi="$1" tgt
    tgt="$(abi_target "$abi")" || return 1
    info "==== ABI $abi (Configure target $tgt) ===="
    local build_dir="$BUILD_ROOT/$abi"
    local install_dir="$INSTALL_ROOT/$abi"
    rm -rf "$build_dir" "$install_dir"; mkdir -p "$build_dir" "$install_dir"
    rsync -a --delete "$SRC_DIR/openssl-${OPENSSL_VERSION}/" "$build_dir/"
    pushd "$build_dir" >/dev/null
    export ANDROID_NDK_ROOT="$NDK_PATH" ANDROID_NDK_HOME="$NDK_PATH"
    export PATH="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
    perl ./Configure "$tgt" \
        -D__ANDROID_API__=$MIN_API_LEVEL \
        --prefix="$install_dir" \
        --openssldir="$install_dir" \
        no-shared no-tests no-apps no-docs
    [[ -f Makefile ]] || { err "Configure failed for $abi"; exit 1; }
    make -j"$(nproc)" build_libs
    make install_dev
    [[ -f "$install_dir/lib/libssl.a" && -f "$install_dir/lib/libcrypto.a" ]] || { err "Missing libs for $abi"; exit 1; }
    popd >/dev/null
}

download
extract
for a in "${ABIS[@]}"; do build_one "$a"; done

echo "\nSummary:"; for a in "${ABIS[@]}"; do ls -1 "$INSTALL_ROOT/$a/lib" 2>/dev/null || true; done
echo "\nDone. Place following in CMakeLists (already patched normally):\n  set(OPENSSL_ROOT_DIR \"${INSTALL_ROOT}/<ABI>\")" 
echo "Libraries installed under: $INSTALL_ROOT/<ABI>/lib"
echo "Headers  installed under: $INSTALL_ROOT/<ABI>/include"
echo "OK"
