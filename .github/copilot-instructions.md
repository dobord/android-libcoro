# Copilot instructions for android-libcoro

This repository uses C++20 coroutines with libcoro and yaml-cpp. Please follow these project-specific rules when proposing changes or generating code.

## Coroutines and scheduling
- Do NOT implement coroutines as lambdas with captures. Prefer free functions (or static member functions) with explicit parameters.
- Every coroutine MUST begin with:
  - `co_await scheduler->schedule();` to ensure execution on the io_scheduler context before any I/O or blocking operations.
- When spawning background work, use the existing scheduler and pass pointers/references to long‑lived objects owned by the caller; do not construct a new scheduler inside a coroutine.
- Example pattern:
  ```cpp
  coro::task<void> do_something(std::shared_ptr<coro::io_scheduler> scheduler,
                                coro::net::udp::peer* p,
                                SomeState* state) {
    co_await scheduler->schedule();
    // ... coroutine body ...
  }

  // Spawning:
  scheduler->spawn(do_something(scheduler, &peer, &state));
  ```

## Networking (libcoro)
- TCP send loop: always poll for write, send, then flush any `rest` until empty; handle `would_block`.
  ```cpp
  std::span<const char> sp{...};
  co_await client.poll(coro::poll_op::write);
  auto [s, rest] = client.send(sp);
  if (s != coro::net::send_status::ok) co_return;
  while (!rest.empty()) {
    co_await client.poll(coro::poll_op::write);
    std::tie(s, rest) = client.send(rest);
    if (s != coro::net::send_status::ok && s != coro::net::send_status::would_block) co_return;
  }
  ```
- UDP receive: `poll(read)` → `recvfrom(buf)`; ignore `would_block` and continue. For replies, `poll(write)` then `sendto`.
- IPv4 addresses stored in frames are in network byte order. To build `coro::net::ip_address` from `uint32_t` in frames, convert via dotted string helpers to avoid endianness mistakes:
  ```cpp
  dst.address = coro::net::ip_address::from_string(udp2tcp::ipv4_to_string(frame.dst_ip));
  dst.port = ntohs(frame.dst_port);
  ```

## Framing protocol
- 4‑byte big‑endian length prefix precedes the body.
- Body header layout (v1): version(1), type(1), flags(2), dst_ip(4), dst_port(2), src_ip(4), src_port(2), reserved(2), then payload.
- Use `udp2tcp::encode`/`decode_one`. Enforce size limits with `cfg.limits.max_frame_bytes`.

## Error handling
- Don’t use `co_await` inside `catch` blocks. Handle errors via status checks and return early on fatal errors.
- Treat `would_block` as a non‑fatal retry signal after a `poll()`.

## Build and deps
- CMake standard is C++20; do not lower.

## Style specifics
- Avoid blocking syscalls without prior `poll()`; prefer non‑blocking and cooperative scheduling.
- When adding TLS client logic, guard with `#ifdef LIBCORO_FEATURE_TLS` and use the awaitable TLS API.
- Project language policy: all source code comments, log messages, commit messages, and documentation MUST be written in English only (no mixed languages) to keep the codebase consistent and accessible. This also applies to build scripts (`build.gradle`, `settings.gradle`, CMake files) and shell/python scripts under `scripts/`.


Following these rules keeps coroutine lifetimes explicit, avoids scheduler mismatches, and prevents subtle endianness and I/O pitfalls.

## Code formatting
- All modifications to C/C++ files (*.c, *.cc, *.cpp, *.cxx, *.h, *.hpp) must be auto-formatted with `clang-format` using the root `.clang-format` file.
- Before finalizing a git commit: run `cmake --build <build_dir> --target format` (or manually `clang-format -i` for the changed files) and ensure `scripts/check_format.sh` passes without errors.
- Non-formatted changes must not be committed.
