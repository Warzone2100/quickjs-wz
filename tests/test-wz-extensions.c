/*
 * Tests for the Warzone 2100 QuickJS extensions (quickjs-wz-extensions/)
 * and WZ-specific API patches (JS_FreeRuntime2).
 *
 * Uses the same JSLimitedContextOptions as WZ's quickjs_backend.cpp
 * (skirmish/multiplayer configuration: eval disabled, no Promise/WeakRef).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "quickjs.h"
#include "quickjs-limitedcontext.h"
#include "quickjs-debugger.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static int g_leak_count = 0;
static void leak_handler(const char *msg)
{
    (void)msg;
    g_leak_count++;
}

static JSContext *new_wz_context(JSRuntime *rt, int allow_eval)
{
    JSLimitedContextOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.baseObjects = 1;
    opts.dateObject = 1;
    opts.eval = allow_eval;
    opts.regExp = 1;
    opts.json = 1;
    opts.proxy = 1;
    opts.mapSet = 1;
    opts.typedArrays = 1;
    opts.promise = 0;
    opts.bigInt = 0;
    opts.weakRef = 0;
    return JS_NewLimitedContext(rt, &opts);
}

static void dump_exception(JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    fprintf(stderr, "  exception: %s\n", str ? str : "<unknown>");
    if (str)
        JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, exc);
}

/* Evaluate via the bypass function; returns the result (caller frees) */
static JSValue eval_bypass(JSContext *ctx, const char *code)
{
    JSValue v = JS_Eval_BypassLimitedContext(ctx, code, strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v))
        dump_exception(ctx);
    return v;
}

static int eval_int(JSContext *ctx, const char *code, int32_t *out)
{
    JSValue v = eval_bypass(ctx, code);
    int ret = -1;
    if (!JS_IsException(v))
        ret = JS_ToInt32(ctx, out, v);
    JS_FreeValue(ctx, v);
    return ret;
}

static int str_eq(JSContext *ctx, JSValueConst v, const char *expected)
{
    const char *s = JS_ToCString(ctx, v);
    int eq = s && strcmp(s, expected) == 0;
    if (!eq)
        fprintf(stderr, "  got \"%s\", expected \"%s\"\n", s ? s : "<null>", expected);
    if (s)
        JS_FreeCString(ctx, s);
    return eq;
}

/* ---- limited context ---- */

static void test_limited_context(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    int32_t i = 0;
    JSValue v;

    CHECK(ctx != NULL);

    /* intrinsics that should be present */
    CHECK(eval_int(ctx, "typeof Date === 'function' && typeof JSON === 'object' && typeof Proxy === 'function' "
                        "&& typeof Map === 'function' && typeof Uint8Array === 'function' "
                        "&& /a+/.test('caaa') ? 1 : 0", &i) == 0 && i == 1);
    CHECK(eval_int(ctx, "JSON.parse(JSON.stringify({a:[1,2,3]})).a[2]", &i) == 0 && i == 3);
    CHECK(eval_int(ctx, "Math.floor(Math.sqrt(1e6)) + Math.abs(-1) + Math.trunc(2.7)", &i) == 0 && i == 1003);

    /* intrinsics that should be absent */
    CHECK(eval_int(ctx, "typeof Promise === 'undefined' ? 1 : 0", &i) == 0 && i == 1);
    CHECK(eval_int(ctx, "typeof WeakRef === 'undefined' ? 1 : 0", &i) == 0 && i == 1);

    /* eval disabled: indirect / direct eval must throw, bypass must still work */
    v = JS_Eval_BypassLimitedContext(ctx, "eval('1+1')", 11, "<test>", JS_EVAL_TYPE_GLOBAL);
    CHECK(JS_IsException(v));
    if (JS_IsException(v))
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, v);

    /* compile-only + JS_EvalFunction (how WZ loads scripts) */
    v = JS_Eval_BypassLimitedContext(ctx, "var loaded = 40 + 2;", 20, "<script>",
                                     JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    CHECK(!JS_IsException(v));
    v = JS_EvalFunction(ctx, v);
    CHECK(!JS_IsException(v));
    JS_FreeValue(ctx, v);
    CHECK(eval_int(ctx, "loaded", &i) == 0 && i == 42);

    JS_FreeContext(ctx);

    /* eval enabled (campaign configuration) */
    ctx = new_wz_context(rt, 1);
    CHECK(eval_int(ctx, "eval('20 + 22')", &i) == 0 && i == 42);
    JS_FreeContext(ctx);

    g_leak_count = 0;
    JS_FreeRuntime2(rt, leak_handler);
    CHECK(g_leak_count == 0);
}

/* ---- JS_GetGlobalLexicalOrVar ---- */

static void test_global_lexical_or_var(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    JSValue v;
    int32_t i = 0;

    v = eval_bypass(ctx,
        "let letVar = 1; const constVar = 2; class ClassVar { static x() { return 3; } }\n"
        "var varVar = 4; function funcVar() { return 5; }\n"
        "globalThis.propVar = 6;\n");
    CHECK(!JS_IsException(v));
    JS_FreeValue(ctx, v);

#define LOOKUP(name) JS_GetGlobalLexicalOrVar(ctx, name, strlen(name))

    v = LOOKUP("letVar");   CHECK(JS_ToInt32(ctx, &i, v) == 0 && i == 1); JS_FreeValue(ctx, v);
    v = LOOKUP("constVar"); CHECK(JS_ToInt32(ctx, &i, v) == 0 && i == 2); JS_FreeValue(ctx, v);
    v = LOOKUP("ClassVar"); CHECK(JS_IsFunction(ctx, v));                 JS_FreeValue(ctx, v);
    v = LOOKUP("varVar");   CHECK(JS_ToInt32(ctx, &i, v) == 0 && i == 4); JS_FreeValue(ctx, v);
    v = LOOKUP("funcVar");  CHECK(JS_IsFunction(ctx, v));                 JS_FreeValue(ctx, v);
    v = LOOKUP("propVar");  CHECK(JS_ToInt32(ctx, &i, v) == 0 && i == 6); JS_FreeValue(ctx, v);

    /* missing, and inherited (must not be found) */
    v = LOOKUP("doesNotExist"); CHECK(JS_IsUndefined(v)); JS_FreeValue(ctx, v);
    v = LOOKUP("toString");     CHECK(JS_IsUndefined(v)); JS_FreeValue(ctx, v);

    /* lexical shadows nothing on the global object */
    v = eval_bypass(ctx, "typeof globalThis.letVar");
    CHECK(str_eq(ctx, v, "undefined"));
    JS_FreeValue(ctx, v);

    /* no pending exception left behind */
    CHECK(!JS_HasException(ctx));

    /* NULL handling */
    v = JS_GetGlobalLexicalOrVar(ctx, NULL, 0);
    CHECK(JS_IsUndefined(v));

#undef LOOKUP

    JS_FreeContext(ctx);
    g_leak_count = 0;
    JS_FreeRuntime2(rt, leak_handler);
    CHECK(g_leak_count == 0);
}

/* ---- JS_Get/SetRandomState ---- */

#define RAND_SAMPLES 5

static void sample_random(JSContext *ctx, double out[RAND_SAMPLES])
{
    int k;
    for (k = 0; k < RAND_SAMPLES; k++) {
        JSValue v = eval_bypass(ctx, "Math.random()");
        out[k] = -1;
        JS_ToFloat64(ctx, &out[k], v);
        JS_FreeValue(ctx, v);
    }
}

static void test_random_state(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    JSContext *ctx2 = new_wz_context(rt, 0);
    double a[RAND_SAMPLES], b[RAND_SAMPLES];
    uint64_t saved;
    int k;

    /*
     * Golden values: Math.random() sequence for state 0x123456789abcdef0.
     * WZ serializes this state into save games, so if upstream ever changes
     * the PRNG (or its double conversion), this test must flag it.
     */
    static const double golden[RAND_SAMPLES] = {
        0.71867385710896148, 0.26170296615520572, 0.51314423328915826,
        0.087864458365633613, 0.078674494350531976
    };

    JS_SetRandomState(ctx, UINT64_C(0x123456789abcdef0));
    CHECK(JS_GetRandomState(ctx) == UINT64_C(0x123456789abcdef0));
    sample_random(ctx, a);
    for (k = 0; k < RAND_SAMPLES; k++) {
        CHECK(a[k] >= 0.0 && a[k] < 1.0);
        if (a[k] != golden[k])
            fprintf(stderr, "  random[%d] = %.17g, expected %.17g\n", k, a[k], golden[k]);
        CHECK(a[k] == golden[k]);
    }

    /* save / restore mid-sequence continues identically */
    saved = JS_GetRandomState(ctx);
    sample_random(ctx, a);
    JS_SetRandomState(ctx, saved);
    sample_random(ctx, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0);

    /* state transfers between contexts */
    JS_SetRandomState(ctx2, saved);
    sample_random(ctx2, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0);

    /* zero state is rejected (would produce an all-zero stream) */
    JS_SetRandomState(ctx, 0);
    CHECK(JS_GetRandomState(ctx) != 0);
    sample_random(ctx, a);
    CHECK(!(a[0] == 0 && a[1] == 0 && a[2] == 0));

    /* NULL handling */
    CHECK(JS_GetRandomState(NULL) == 0);
    JS_SetRandomState(NULL, 1);

    JS_FreeContext(ctx2);
    JS_FreeContext(ctx);
    g_leak_count = 0;
    JS_FreeRuntime2(rt, leak_handler);
    CHECK(g_leak_count == 0);
}

/* ---- debugger extensions ---- */

static JSValue js_caller_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return js_debugger_get_caller_name(ctx);
}

static JSValue js_caller_func(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return js_debugger_get_caller_funcObject(ctx);
}

static JSValue js_backtrace(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return js_debugger_build_backtrace(ctx, NULL);
}

/*
 * Note the semantics: "caller" is two frames up from the native function,
 * i.e. the caller of the JS function that invoked the native function.
 */
#define DBG_SRC \
    "function wantsCallerName() { return callerName(); }\n" \
    "function namedCaller() { return wantsCallerName(); }\n" \
    "function inner() { return backtrace(); }\n" \
    "function outer() { return inner(); }\n" \
    "function wantsCallerFunc() { return callerFunc(); }\n" \
    "function funcCaller() { return wantsCallerFunc(); }\n"

static void test_debugger(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue v;
    int32_t i = 0;

    JS_SetPropertyStr(ctx, global, "callerName", JS_NewCFunction(ctx, js_caller_name, "callerName", 0));
    JS_SetPropertyStr(ctx, global, "callerFunc", JS_NewCFunction(ctx, js_caller_func, "callerFunc", 0));
    JS_SetPropertyStr(ctx, global, "backtrace", JS_NewCFunction(ctx, js_backtrace, "backtrace", 0));
    JS_FreeValue(ctx, global);

    v = JS_Eval_BypassLimitedContext(ctx,
        DBG_SRC, strlen(DBG_SRC), "dbgtest.js", JS_EVAL_TYPE_GLOBAL);
    CHECK(!JS_IsException(v));
    JS_FreeValue(ctx, v);

    v = eval_bypass(ctx, "namedCaller()");
    CHECK(str_eq(ctx, v, "namedCaller"));
    JS_FreeValue(ctx, v);

    v = eval_bypass(ctx, "(function() { return wantsCallerName(); })()");
    CHECK(str_eq(ctx, v, "<anonymous>"));
    JS_FreeValue(ctx, v);

    CHECK(eval_int(ctx, "funcCaller() === funcCaller ? 1 : 0", &i) == 0 && i == 1);

    v = eval_bypass(ctx, "JSON.stringify(outer().map(f => [f.name, f.filename, f.line]))");
    CHECK(str_eq(ctx, v,
        "[[\"(native)\",null,null],"
        "[\"inner\",\"dbgtest.js\",3],"
        "[\"outer\",\"dbgtest.js\",4],"
        "[\"<eval>\",\"<test>\",1]]"));
    JS_FreeValue(ctx, v);

    JS_FreeContext(ctx);
    g_leak_count = 0;
    JS_FreeRuntime2(rt, leak_handler);
    CHECK(g_leak_count == 0);
}

/* ---- JS_FreeRuntime2 leak reporting ---- */

static void test_free_runtime2_reports_leak(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    /* deliberately leak an object */
    JSValue leaked = JS_NewObject(ctx);
    (void)leaked;
    JS_FreeContext(ctx);
    g_leak_count = 0;
    JS_FreeRuntime2(rt, leak_handler); /* must not assert/abort */
    CHECK(g_leak_count > 0);
}

int main(void)
{
    test_limited_context();
    test_global_lexical_or_var();
    test_random_state();
    test_debugger();
    test_free_runtime2_reports_leak();

    printf("test-wz-extensions: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
