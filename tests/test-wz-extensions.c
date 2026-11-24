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
#include "quickjs-serialization.h"

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

/* ---- Map / Set / Date internal state (quickjs-serialization) ---- */

/* The serialization API, exposed to the test scripts as __ser.* */
static JSValue ser_kind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return JS_NewInt32(ctx, (int32_t)JS_GetBuiltinObjectKind(argv[0]));
}

/* [[key, value], ...] */
static JSValue ser_entries(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSValue *entries = NULL, arr;
    uint32_t count = 0, i;
    (void)this_val; (void)argc;
    if (JS_GetMapOrSetEntries(ctx, argv[0], &entries, &count))
        return JS_EXCEPTION;
    arr = JS_NewArray(ctx);
    for (i = 0; i < count; i++) {
        JSValue pair = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, pair, 0, JS_DupValue(ctx, entries[2 * i]));
        JS_SetPropertyUint32(ctx, pair, 1, JS_DupValue(ctx, entries[2 * i + 1]));
        JS_SetPropertyUint32(ctx, arr, i, pair);
    }
    JS_FreeMapOrSetEntries(ctx, entries, count);
    return arr;
}

static JSValue ser_new_map_or_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int32_t kind = 0;
    (void)this_val; (void)argc;
    if (JS_ToInt32(ctx, &kind, argv[0]))
        return JS_EXCEPTION;
    return JS_NewMapOrSet(ctx, (JSBuiltinObjectKind)kind);
}

static JSValue ser_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    if (JS_MapOrSetAdd(ctx, argv[0], argv[1], argv[2]))
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

static JSValue ser_get_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    double t = 0;
    (void)this_val; (void)argc;
    if (JS_GetDateTimeValue(ctx, argv[0], &t))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, t);
}

static JSValue ser_set_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    double t = 0;
    (void)this_val; (void)argc;
    if (JS_ToFloat64(ctx, &t, argv[1]) || JS_SetDateTimeValue(ctx, argv[0], t))
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

static JSValue ser_gc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

/* Helpers for the checks. The intrinsic iterators / size getters are captured up front, so that the
   checks still see the real state after some of them monkeypatch the prototypes. */
static const char ser_prelude[] =
    "var K = { OTHER: 0, MAP: 1, SET: 2, WEAKMAP: 3, WEAKSET: 4, DATE: 5 };\n"
    "var MapIter = Map.prototype[Symbol.iterator], SetIter = Set.prototype[Symbol.iterator];\n"
    "var mapSize = Object.getOwnPropertyDescriptor(Map.prototype, 'size').get;\n"
    "var setSize = Object.getOwnPropertyDescriptor(Set.prototype, 'size').get;\n"
    "var dateGetTime = Date.prototype.getTime;\n"
    /* observable state: size, entries in iteration order (SameValue), prototype */
    "function snapshot(m) {\n"
    "  var isSet = __ser.kind(m) === K.SET, it = (isSet ? SetIter : MapIter).call(m), out = [];\n"
    "  for (var r = it.next(); !r.done; r = it.next()) out.push(isSet ? [r.value, r.value] : r.value);\n"
    "  return { size: (isSet ? setSize : mapSize).call(m), entries: out, proto: Object.getPrototypeOf(m) };\n"
    "}\n"
    "function sameState(a, b) {\n"
    "  var sa = snapshot(a), sb = snapshot(b);\n"
    "  return sa.size === sb.size && sa.proto === sb.proto && sa.entries.length === sb.entries.length\n"
    "    && sa.entries.every(function (e, i) { return Object.is(e[0], sb.entries[i][0]) && Object.is(e[1], sb.entries[i][1]); });\n"
    "}\n"
    /* what a host restore does: an empty Map / Set, the entries in order, then the prototype */
    "function roundTrip(m) {\n"
    "  var r = __ser.newMapOrSet(__ser.kind(m));\n"
    "  __ser.entries(m).forEach(function (e) { __ser.add(r, e[0], e[1]); });\n"
    "  if (Object.getPrototypeOf(r) !== Object.getPrototypeOf(m)) Object.setPrototypeOf(r, Object.getPrototypeOf(m));\n"
    "  return r;\n"
    "}\n"
    "function throws(f) { try { f(); return false; } catch (e) { return true; } }\n";

/* Each check is a function body that returns true */
static const char *const ser_checks[] = {
    /* classification by internal class */
    "return __ser.kind(new Map()) === K.MAP && __ser.kind(new Set()) === K.SET && __ser.kind(new Date()) === K.DATE"
    " && __ser.kind(new WeakMap()) === K.WEAKMAP && __ser.kind(new WeakSet()) === K.WEAKSET;",
    "return [1, 'x', null, undefined, true, {}, [], function () {}].every(function (v) { return __ser.kind(v) === K.OTHER; });",
    "return __ser.kind(Object.create(Map.prototype)) === K.OTHER;", /* no [[MapData]] */
    "var m0 = new Map(); Object.setPrototypeOf(m0, Object.prototype); return __ser.kind(m0) === K.MAP;",
    "return __ser.kind(new Proxy(new Map(), {})) === K.OTHER;", /* never the target, and no trap runs */
    "class SubMap extends Map {} return __ser.kind(new SubMap()) === K.MAP;",

    /* iteration order after deletes, re-adds and overwrites, and lockstep afterwards */
    "var a = {}, b = {}, m = new Map();"
    " m.set('x', 1).set(a, 2).set(3, b).set('y', 4);"
    " m.delete(a); m.set(a, 5); m.set('x', 6); m.delete(3); m.set(3, 7);"
    " var r = roundTrip(m), ok = sameState(m, r);"
    " [m, r].forEach(function (mm) { mm.delete('x'); mm.set('z', 0); mm.set('x', 1); mm.delete(a); });"
    " return ok && sameState(m, r);",

    /* a large map, mostly deleted (the hash table only grows; nothing observable differs) */
    "var big = new Map(), i; for (i = 0; i < 5000; i++) big.set('k' + i, i);"
    " for (i = 0; i < 5000; i++) if (i % 1000) big.delete('k' + i);"
    " return big.size === 5 && sameState(big, roundTrip(big));",

    /* -0 is stored as +0, NaN keys, object keys keep their identity, self references */
    "var o = { tag: 1 }, m = new Map(); m.set(-0, 'z').set(NaN, 'n').set(o, o).set('self', m); m.set(m, 'selfkey');"
    " var r = roundTrip(m), e = __ser.entries(m);"
    " return Object.is(e[0][0], 0) && r.get(NaN) === 'n' && r.get(o) === o && r.get('self') === m && r.get(m) === 'selfkey';",

    /* deleted records an iterator still points at ("zombies") are skipped */
    "var m = new Map([['a', 1], ['b', 2], ['c', 3]]), it = MapIter.call(m); it.next(); m.delete('a');"
    " var e = __ser.entries(m); return e.length === 2 && e[0][0] === 'b' && sameState(m, roundTrip(m));",

    /* Set, and a Set's values come back as undefined */
    "var s = new Set([5, 'five', -0, NaN]); s.delete(5); s.add(5); s.add('five');"
    " return sameState(s, roundTrip(s)) && __ser.entries(s)[0][1] === undefined;",

    /* subclass instances: same internal kind, prototype re-attached by the host */
    "class Registry extends Map { lookup(k) { return this.has(k) ? this.get(k) : 'none'; } }"
    " var m = new Registry([['a', 1]]), r = roundTrip(m);"
    " return r instanceof Registry && r.lookup('a') === 1 && r.lookup('q') === 'none' && sameState(m, r);",

    /* script-visible methods are never called: monkeypatching them changes nothing */
    "var m = new Map([['a', 1], ['b', 2]]), r;"
    " var saved = [Map.prototype.forEach, Map.prototype.set, Map.prototype.entries, Map.prototype[Symbol.iterator]];"
    " Map.prototype.forEach = Map.prototype.set = function () { throw new Error('hijacked'); };"
    " Map.prototype.entries = Map.prototype[Symbol.iterator] = function* () { yield ['evil', 666]; };"
    " var ok = !throws(function () { r = roundTrip(m); });"
    " Map.prototype.forEach = saved[0]; Map.prototype.set = saved[1]; Map.prototype.entries = saved[2]; Map.prototype[Symbol.iterator] = saved[3];"
    " return ok && sameState(m, r);",
    "var saved = Date.prototype.getTime; Date.prototype.getTime = function () { return 0; };"
    " var t = __ser.getTime(new Date(99)); Date.prototype.getTime = saved; return t === 99;",

    /* weak collections are classified, but not read or created */
    "return throws(function () { __ser.entries(new WeakMap()); }) && throws(function () { __ser.entries(new WeakSet()); })"
    " && throws(function () { __ser.newMapOrSet(K.WEAKMAP); }) && throws(function () { __ser.add(new WeakMap(), {}, 1); });",

    /* the snapshot stays valid after the source is cleared */
    "var m = new Map(), i; for (i = 0; i < 100; i++) m.set({ i: i }, { v: i });"
    " var snap = __ser.entries(m); m.clear(); __ser.gc();"
    " return snap.length === 100 && snap[99][0].i === 99 && snap[99][1].v === 99;",
    "return __ser.entries(new Map()).length === 0;",

    /* add(): Map.prototype.set / Set.prototype.add semantics */
    "var r = __ser.newMapOrSet(K.MAP); __ser.add(r, 'a', 1); __ser.add(r, 'b', 2); __ser.add(r, 'a', 3); __ser.add(r, -0, 'z');"
    " return JSON.stringify(Array.from(MapIter.call(r))) === '[[\"a\",3],[\"b\",2],[0,\"z\"]]' && Object.is(Array.from(r.keys())[2], 0);",
    "var s = __ser.newMapOrSet(K.SET); __ser.add(s, 'x', 'ignored'); return s.has('x') && __ser.entries(s)[0][1] === undefined;",
    "return Object.getPrototypeOf(__ser.newMapOrSet(K.MAP)) === Map.prototype && Object.getPrototypeOf(__ser.newMapOrSet(K.SET)) === Set.prototype;",

    /* everything else is rejected with an exception */
    "return [function () { __ser.entries({}); }, function () { __ser.entries([]); }, function () { __ser.add({}, 1, 2); },"
    " function () { __ser.getTime({}); }, function () { __ser.setTime(new Map(), 1); },"
    " function () { __ser.newMapOrSet(K.DATE); }, function () { __ser.newMapOrSet(K.OTHER); }].every(throws);",

    /* Date time values, read and written in place, with TimeClip */
    "return [0, -1, 1700000000123, 8.64e15, -8.64e15].every(function (v) { return __ser.getTime(new Date(v)) === v; })"
    " && Number.isNaN(__ser.getTime(new Date(NaN)));",
    "var d = new Date(5), same = d; __ser.setTime(d, 1.9); var ok = same.getTime() === 1;"
    " __ser.setTime(d, -0.5); ok = ok && Object.is(d.getTime(), 0);"
    " __ser.setTime(d, 9e15); ok = ok && Number.isNaN(d.getTime());"
    " __ser.setTime(d, 8.64e15); return ok && dateGetTime.call(d) === 8.64e15;",
    "class Stamp extends Date { label() { return 'T' + this.getTime(); } }"
    " var s = new Stamp(1234); return __ser.kind(s) === K.DATE && __ser.getTime(s) === 1234 && s.label() === 'T1234';",
};

static void test_serialization(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = new_wz_context(rt, 0);
    JSValue global = JS_GetGlobalObject(ctx), ser = JS_NewObject(ctx), v;
    size_t i;

    JS_SetPropertyStr(ctx, ser, "kind", JS_NewCFunction(ctx, ser_kind, "kind", 1));
    JS_SetPropertyStr(ctx, ser, "entries", JS_NewCFunction(ctx, ser_entries, "entries", 1));
    JS_SetPropertyStr(ctx, ser, "newMapOrSet", JS_NewCFunction(ctx, ser_new_map_or_set, "newMapOrSet", 1));
    JS_SetPropertyStr(ctx, ser, "add", JS_NewCFunction(ctx, ser_add, "add", 3));
    JS_SetPropertyStr(ctx, ser, "getTime", JS_NewCFunction(ctx, ser_get_time, "getTime", 1));
    JS_SetPropertyStr(ctx, ser, "setTime", JS_NewCFunction(ctx, ser_set_time, "setTime", 2));
    JS_SetPropertyStr(ctx, ser, "gc", JS_NewCFunction(ctx, ser_gc, "gc", 0));
    JS_SetPropertyStr(ctx, global, "__ser", ser);
    JS_FreeValue(ctx, global);

    v = eval_bypass(ctx, ser_prelude);
    CHECK(!JS_IsException(v));
    JS_FreeValue(ctx, v);

    for (i = 0; i < sizeof(ser_checks) / sizeof(ser_checks[0]); i++) {
        /* each check in its own function, so they can reuse names */
        size_t size = strlen(ser_checks[i]) + 64;
        char *code = (char *)malloc(size);
        int32_t ok = 0;
        CHECK(code != NULL);
        if (!code)
            continue;
        snprintf(code, size, "(function () { %s })() ? 1 : 0", ser_checks[i]);
        if (eval_int(ctx, code, &ok) != 0 || ok != 1)
            fprintf(stderr, "  serialization check %d failed: %s\n", (int)i, ser_checks[i]);
        CHECK(ok == 1);
        free(code);
    }

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
    test_serialization();
    test_free_runtime2_reports_leak();

    printf("test-wz-extensions: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
