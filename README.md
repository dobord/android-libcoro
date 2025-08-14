# android-libcoro

Sample Android project integrating [libcoro](external/libcoro) with networking + TLS features and a statically built OpenSSL.

## Requirements
- Android SDK / NDK (see `ndkVersion` in `app/build.gradle`)
- CMake / Ninja (via the Android Gradle Plugin)
- Git submodules (`git submodule update --init --recursive`)

## OpenSSL
Run `build_openssl.sh` to produce static libraries under `external/openssl/<ABI>`. Ensure the directory for each enabled ABI (in `abiFilters`) exists before building the app.

## Build
```
./gradlew :app:assembleDebug
```
The APK will be in `app/build/outputs/apk/debug/`.

## Code formatting (clang-format)
The root `.clang-format` (LLVM based with customizations) governs all C/C++ sources. Always format modified or newly added files before committing.

### Quick manual format
```
clang-format -i app/src/main/cpp/*.cpp
clang-format -i external/libcoro/include/coro/*.hpp
```
Add more paths as needed.

### Bulk formatting
```
find app/src/main/cpp -name "*.c*" -o -name "*.h*" | xargs clang-format -i
find external/libcoro/include/coro -name "*.hpp" | xargs clang-format -i
```

### Optional Git hook
Create `.git/hooks/pre-commit`:
```
#!/usr/bin/env bash
CHANGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|cc|cpp|cxx|h|hpp)$' || true)
[ -z "$CHANGED" ] && exit 0
for f in $CHANGED; do
  [ -f "$f" ] || continue
  clang-format -i "$f"
  git add "$f"
done
```
Make it executable:
```
chmod +x .git/hooks/pre-commit
```

## Logging
Macros `LOGE` / `LOGI` in `main.cpp` wrap `__android_log_print`. Extract them to a dedicated header (e.g. `log.hpp`) if reused broadly.

## IntelliSense
For accurate conditional compilation highlighting generate `compile_commands.json` (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) and point your editor to it. A minimal fallback defines feature macros for IntelliSense inside `main.cpp` only.

## Language policy
All comments, log strings, commit messages, and documentation must be written in English only (see `.github/copilot-instructions.md`).
This also applies to build scripts (`build.gradle`, `settings.gradle`, CMake files) and shell/python scripts under `scripts/`.

## Licenses
Consult third‑party component directories (`external/libcoro`, `external/libcoro/vendor/c-ares`, OpenSSL) for their respective licenses.
