/*
 * C++ consumer test for quickjs-wz.
 *
 * Warzone 2100 includes quickjs.h (and the WZ extension headers) from C++, so this test verifies that:
 *  - the public headers compile cleanly as C++ with WZ's warning flags (and
 *    warnings as errors), included the same way WZ does, which also
 *    exercises the C++ fallbacks in patch 003 (JS_MKVAL / JS_MKPTR / JS_NAN,
 *    JS_NewCFunctionMagic) and the const-cast helpers in patch 006
 *  - JSValues built by the inline header functions in C++ agree with what the
 *    C library expects, and vice versa (C/C++ JSValue representation match)
 */

// Include the headers the same way WZ does (see src/quickjs_backend.cpp)
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#if defined(__clang__) && defined(__clang_major__) && __clang_major__ >= 19
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4191 ) // 'type cast': unsafe conversion from 'JSCFunctionMagic (__cdecl *)' to 'JSCFunction (__cdecl *)'
#endif
#include "quickjs.h"
#include "quickjs-limitedcontext.h"
#include "quickjs-debugger.h"
#if defined(_MSC_VER)
#pragma warning( pop )
#endif
#if defined(__clang__) && defined(__clang_major__) && __clang_major__ >= 19
#pragma clang diagnostic pop
#endif
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static JSValue js_magic_add(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv, int magic)
{
    int32_t a = 0;
    if (argc < 1 || JS_ToInt32(ctx, &a, argv[0]))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, a + magic);
}

static std::string to_std_string(JSContext *ctx, JSValueConst v)
{
    const char *s = JS_ToCString(ctx, v);
    std::string result = s ? s : "<null>";
    JS_FreeCString(ctx, s);
    return result;
}

int main()
{
    JSRuntime *rt = JS_NewRuntime();
    JSLimitedContextOptions opts = {};
    opts.baseObjects = 1;
    opts.json = 1;
    JSContext *ctx = JS_NewLimitedContext(rt, &opts);
    CHECK(ctx != nullptr);

    // values constructed by header macros / inline functions in C++
    JSValue vUndef = JS_UNDEFINED;
    JSValue vNull = JS_NULL;
    JSValue vTrue = JS_TRUE;
    JSValue vNan = JS_NAN;
    JSValue vInt = JS_MKVAL(JS_TAG_INT, 1234);
    JSValue vDbl = JS_NewFloat64(ctx, 2.5);

    CHECK(JS_IsUndefined(vUndef));
    CHECK(JS_IsNull(vNull));
    CHECK(JS_IsBool(vTrue) && JS_VALUE_GET_BOOL(vTrue) == 1);
    CHECK(JS_VALUE_GET_NORM_TAG(vNan) == JS_TAG_FLOAT64);
    CHECK(JS_VALUE_GET_NORM_TAG(vDbl) == JS_TAG_FLOAT64);
    CHECK(JS_VALUE_GET_TAG(vInt) == JS_TAG_INT && JS_VALUE_GET_INT(vInt) == 1234);

    // pass C++-constructed values into the C library
    double d = 0;
    CHECK(JS_ToFloat64(ctx, &d, vNan) == 0 && std::isnan(d));
    CHECK(JS_ToFloat64(ctx, &d, vDbl) == 0 && d == 2.5);
    int32_t i = 0;
    CHECK(JS_ToInt32(ctx, &i, vInt) == 0 && i == 1234);
    CHECK(to_std_string(ctx, vNull) == "null");

    // values produced by the C library, inspected with C++ inline header code
    const char *src = "({ s: 'str', n: 0.5, i: 7, o: { x: [1, 2] } })";
    JSValue obj = JS_Eval_BypassLimitedContext(ctx, src, std::strlen(src), "<cxx>", JS_EVAL_TYPE_GLOBAL);
    CHECK(JS_IsObject(obj));
    JSValue s = JS_GetPropertyStr(ctx, obj, "s");
    JSValue n = JS_GetPropertyStr(ctx, obj, "n");
    JSValue iv = JS_GetPropertyStr(ctx, obj, "i");
    JSValue o = JS_GetPropertyStr(ctx, obj, "o");
    CHECK(JS_IsString(s) && to_std_string(ctx, s) == "str");
    CHECK(JS_VALUE_GET_NORM_TAG(n) == JS_TAG_FLOAT64 && JS_VALUE_GET_FLOAT64(n) == 0.5);
    CHECK(JS_VALUE_GET_TAG(iv) == JS_TAG_INT && JS_VALUE_GET_INT(iv) == 7);
    CHECK(JS_IsObject(o));

    // ref counting via inline JS_DupValue / JS_FreeValue across the boundary
    JSValue o2 = JS_DupValue(ctx, o);
    CHECK(JS_VALUE_GET_PTR(o2) == JS_VALUE_GET_PTR(o));
    JS_FreeValue(ctx, o2);

    JSValue json = JS_JSONStringify(ctx, o, JS_UNDEFINED, JS_UNDEFINED);
    CHECK(to_std_string(ctx, json) == "{\"x\":[1,2]}");

    JS_FreeValue(ctx, json);
    JS_FreeValue(ctx, o);
    JS_FreeValue(ctx, iv);
    JS_FreeValue(ctx, n);
    JS_FreeValue(ctx, s);
    JS_FreeValue(ctx, obj);

    // JS_NewCFunctionMagic (C++ fallback in patch 003)
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "add100", JS_NewCFunctionMagic(ctx, js_magic_add, "add100", 1, JS_CFUNC_generic_magic, 100));
    JS_FreeValue(ctx, global);
    const char *call = "add100(23)";
    JSValue r = JS_Eval_BypassLimitedContext(ctx, call, std::strlen(call), "<cxx>", JS_EVAL_TYPE_GLOBAL);
    CHECK(JS_ToInt32(ctx, &i, r) == 0 && i == 123);
    JS_FreeValue(ctx, r);

    JS_SetRandomState(ctx, 42);
    CHECK(JS_GetRandomState(ctx) == 42);

    JS_FreeContext(ctx);
    int leaks = 0;
    static int *leaksPtr = &leaks;
    JS_FreeRuntime2(rt, [](const char *msg) { std::fprintf(stderr, "LEAK: %s\n", msg); (*leaksPtr)++; });
    CHECK(leaks == 0);

    std::printf("test-cxx-consumer: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
