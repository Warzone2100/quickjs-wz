/*
 * Minimal QuickJS test-script runner for quickjs-wz CI.
 *
 * Links only against the qjs library (no quickjs-libc).
 * Provides just enough of a host environment to run the upstream engine tests:
 *   print(), console.log(), gc(), std.gc(), os.platform, os.setTimeout()
 * and fails on unhandled promise rejections.
 *
 * Usage: run-test-script <script.js> [<script.js> ...]
 * Each script runs in a fresh runtime + context.
 * Exit code is non-zero if any script throws, leaves an unhandled exception in a job, or leaks GC objects at JS_FreeRuntime2 time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

#define MAX_TIMERS 64

typedef struct {
    JSValue func;
    double delay;
    int seq;
} Timer;

typedef struct {
    Timer timers[MAX_TIMERS];
    int timer_count;
    int timer_seq;
    int unhandled_rejections;
} HostState;

static int g_leak_detected = 0;

static void leak_handler(const char *msg)
{
    fprintf(stderr, "LEAK: %s\n", msg);
    g_leak_detected = 1;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fputs(str, stdout);
        if (i + 1 < argc)
            fputc(' ', stdout);
        JS_FreeCString(ctx, str);
    }
    fputc('\n', stdout);
    fflush(stdout);
    return JS_UNDEFINED;
}

static JSValue js_gc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static JSValue js_set_timeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    HostState *hs = (HostState *)JS_GetContextOpaque(ctx);
    double delay = 0;
    Timer *t;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "setTimeout: not a function");
    if (argc >= 2 && JS_ToFloat64(ctx, &delay, argv[1]))
        return JS_EXCEPTION;
    if (hs->timer_count >= MAX_TIMERS)
        return JS_ThrowInternalError(ctx, "setTimeout: too many timers");
    t = &hs->timers[hs->timer_count++];
    t->func = JS_DupValue(ctx, argv[0]);
    t->delay = delay;
    t->seq = hs->timer_seq++;
    return JS_NewInt32(ctx, t->seq);
}

static void dump_exception(JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    fprintf(stderr, "Uncaught exception: %s\n", str ? str : "<unknown>");
    if (str)
        JS_FreeCString(ctx, str);
    if (JS_IsObject(exc)) {
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *s = JS_ToCString(ctx, stack);
            if (s) {
                fprintf(stderr, "%s\n", s);
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, stack);
    }
    JS_FreeValue(ctx, exc);
}

static void promise_rejection_tracker(JSContext *ctx, JSValueConst promise,
                                      JSValueConst reason, JS_BOOL is_handled,
                                      void *opaque)
{
    HostState *hs = (HostState *)opaque;
    if (!is_handled) {
        const char *str = JS_ToCString(ctx, reason);
        fprintf(stderr, "Possibly unhandled promise rejection: %s\n", str ? str : "<unknown>");
        if (str)
            JS_FreeCString(ctx, str);
        hs->unhandled_rejections++;
    } else {
        hs->unhandled_rejections--;
    }
}

/* returns 0 on success, -1 if a job threw */
static int drain_jobs(JSRuntime *rt)
{
    JSContext *job_ctx;
    int ret;
    for (;;) {
        ret = JS_ExecutePendingJob(rt, &job_ctx);
        if (ret == 0)
            return 0;
        if (ret < 0) {
            dump_exception(job_ctx);
            return -1;
        }
    }
}

/* run timers in (delay, insertion) order, draining jobs after each */
static int run_timers(JSRuntime *rt, JSContext *ctx, HostState *hs)
{
    int failed = 0;
    while (hs->timer_count > 0) {
        int i, best = 0;
        Timer t;
        JSValue ret;
        for (i = 1; i < hs->timer_count; i++) {
            if (hs->timers[i].delay < hs->timers[best].delay ||
                (hs->timers[i].delay == hs->timers[best].delay &&
                 hs->timers[i].seq < hs->timers[best].seq))
                best = i;
        }
        t = hs->timers[best];
        hs->timers[best] = hs->timers[--hs->timer_count];
        ret = JS_Call(ctx, t.func, JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, t.func);
        if (JS_IsException(ret)) {
            dump_exception(ctx);
            failed = 1;
        }
        JS_FreeValue(ctx, ret);
        if (drain_jobs(rt))
            failed = 1;
    }
    return failed ? -1 : 0;
}

static char *read_file(const char *filename, size_t *plen)
{
    FILE *f = fopen(filename, "rb");
    char *buf;
    long len;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)len + 1);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    *plen = (size_t)len;
    return buf;
}

static void add_host_globals(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JSValue std_obj = JS_NewObject(ctx);
    JSValue os_obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, global, "print", JS_NewCFunction(ctx, js_print, "print", 1));
    JS_SetPropertyStr(ctx, global, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));

    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JS_SetPropertyStr(ctx, std_obj, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));
    JS_SetPropertyStr(ctx, global, "std", std_obj);

#if defined(_WIN32)
    JS_SetPropertyStr(ctx, os_obj, "platform", JS_NewString(ctx, "win32"));
#else
    JS_SetPropertyStr(ctx, os_obj, "platform", JS_NewString(ctx, "quickjs-wz-test"));
#endif
    JS_SetPropertyStr(ctx, os_obj, "setTimeout", JS_NewCFunction(ctx, js_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "os", os_obj);

    JS_FreeValue(ctx, global);
}

static int run_script(const char *filename)
{
    JSRuntime *rt;
    JSContext *ctx;
    HostState hs;
    char *buf;
    size_t len = 0;
    JSValue val;
    int failed = 0;

    buf = read_file(filename, &len);
    if (!buf) {
        fprintf(stderr, "Could not read: %s\n", filename);
        return -1;
    }

    memset(&hs, 0, sizeof(hs));
    g_leak_detected = 0;
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    if (!rt || !ctx) {
        fprintf(stderr, "Failed to create runtime/context\n");
        free(buf);
        return -1;
    }
    JS_SetContextOpaque(ctx, &hs);
    JS_SetHostPromiseRejectionTracker(rt, promise_rejection_tracker, &hs);
    add_host_globals(ctx);

    val = JS_Eval(ctx, buf, len, filename, JS_EVAL_TYPE_GLOBAL);
    free(buf);
    if (JS_IsException(val)) {
        dump_exception(ctx);
        failed = 1;
    }
    JS_FreeValue(ctx, val);

    if (drain_jobs(rt))
        failed = 1;
    if (run_timers(rt, ctx, &hs))
        failed = 1;
    if (hs.unhandled_rejections > 0) {
        fprintf(stderr, "%d unhandled promise rejection(s)\n", hs.unhandled_rejections);
        failed = 1;
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime2(rt, leak_handler);
    if (g_leak_detected)
        failed = 1;

    printf("%s: %s\n", filename, failed ? "FAILED" : "OK");
    return failed ? -1 : 0;
}

int main(int argc, char **argv)
{
    int i, failures = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <script.js>...\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (run_script(argv[i]))
            failures++;
    }
    return failures ? 1 : 0;
}
