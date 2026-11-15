# QuickJS (modified)

This repo contains [quickjs](https://github.com/bellard/quickjs), with modifications and patches required by Warzone 2100.

For a breakdown of the patches applied for Warzone 2100, see the commit history and/or the [`patches/`](patches/) subdirectory.

For more information on upstream QuickJS, see:
https://github.com/bellard/quickjs

## Tests

When built as the top-level CMake project, the tests in [`tests/`](tests/) are built (use `-DQUICKJS_WZ_BUILD_TESTS=OFF` to disable).

- `upstream.*`: a subset of upstream's `quickjs/tests/*.js` that only need the core engine, run via a minimal host shim (`tests/run-test-script.c`)
- `wz.extensions`: the WZ extensions (limited context, `JS_GetGlobalLexicalOrVar`, `JS_Get/SetRandomState`, debugger helpers, `JS_FreeRuntime2`)
- `wz.cxx*-consumer`: compiles the public headers as C++ (warnings as errors) and checks C/C++ `JSValue` interop

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```
