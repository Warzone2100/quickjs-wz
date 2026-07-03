// This file is directly included in quickjs.c.

/*
 * QuickJS Limited Context Extensions
 *
 * Copyright (c) 2020-2026 Warzone 2100 Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <quickjs-limitedcontext.h>

JSContext *JS_NewLimitedContext(JSRuntime *rt, const JSLimitedContextOptions* options)
{
	JSContext *ctx;

	ctx = JS_NewContextRaw(rt);
	if (!ctx)
		return NULL;

	if (options->baseObjects)
		JS_AddIntrinsicBaseObjects(ctx);
	if (options->dateObject)
		JS_AddIntrinsicDate(ctx);
	if (options->eval)
		JS_AddIntrinsicEval(ctx);	// required for JS_Eval (etc) to work
#if !defined(QUICKJS_NG)
	if (options->stringNormalize)
		JS_AddIntrinsicStringNormalize(ctx);
#endif
	if (options->regExp)
		JS_AddIntrinsicRegExp(ctx);
	if (options->json)
		JS_AddIntrinsicJSON(ctx);
	if (options->proxy)
		JS_AddIntrinsicProxy(ctx);
	if (options->mapSet)
		JS_AddIntrinsicMapSet(ctx);
	if (options->typedArrays)
		JS_AddIntrinsicTypedArrays(ctx);
	if (options->promise)
		JS_AddIntrinsicPromise(ctx);
#if defined(QUICKJS_NG)
	if (options->bigInt)
		JS_AddIntrinsicBigInt(ctx);
#endif
	if (options->weakRef)
		JS_AddIntrinsicWeakRef(ctx);
	return ctx;
}

// Always accessible JS_Eval (even if limited context has eval disabled)
JSValue JS_Eval_BypassLimitedContext(JSContext *ctx, const char *input, size_t input_len,
				const char *filename, int eval_flags)
{
	int eval_type = eval_flags & JS_EVAL_TYPE_MASK;
#if defined(QUICKJS_NG)
	int line = 1;
#endif
	JSValue ret;

	assert(eval_type == JS_EVAL_TYPE_GLOBAL ||
		   eval_type == JS_EVAL_TYPE_MODULE);

#if defined(QUICKJS_NG)
	ret = __JS_EvalInternal(ctx, ctx->global_obj, input, input_len, filename, line,
							  eval_flags, -1);
#else
	ret = __JS_EvalInternal(ctx, ctx->global_obj, input, input_len, filename,
							  eval_flags, -1);
#endif
	return ret;
}

// Resolve a top-level binding by name: the global lexical scope (let/const/class) first,
// then the global object (var/function)
//
// Returns the bound value as a new JSValue reference the caller must free,
// or JS_UNDEFINED if there is no such binding
//
// NOTES:
// Top-level `class`/`let`/`const` are lexical bindings stored in ctx->global_var_obj (*not*
// as global-object properties), which is why a plain JS_GetPropertyStr on the global object
// cannot find them. This performs a pure property lookup.
JSValue JS_GetGlobalLexicalOrVar(JSContext *ctx, const char *name, size_t name_len)
{
	JSPropertyDescriptor desc;
	JSAtom atom;
	JSValue ret = JS_UNDEFINED;
	int found;

	if (!ctx || !name) {
		return JS_UNDEFINED;
	}

	// A limited context created without base objects has no global lexical scope object
	if (!JS_IsObject(ctx->global_var_obj) || !JS_IsObject(ctx->global_obj)) {
		return JS_UNDEFINED;
	}

	atom = JS_NewAtomLen(ctx, name, name_len);
	if (atom == JS_ATOM_NULL) {
		// OOM - clear the pending exception & report unresolved
		JS_FreeValue(ctx, JS_GetException(ctx));
		return JS_UNDEFINED;
	}

	// 1) Global lexical scope: let/const/class. Own property of global_var_obj.
	//    Its value (a var-ref) is dereferenced into desc.value by JS_GetOwnProperty.
	found = JS_GetOwnProperty(ctx, &desc, ctx->global_var_obj, atom);
	if (found == 0) {
		// 2) Global object: var/function declarations. Own-only, thus we should not pick up
		//    inherited members like toString / valueOf from Object.prototype.
		found = JS_GetOwnProperty(ctx, &desc, ctx->global_obj, atom);
	}

	if (found > 0) {
		// desc.value already holds the dereferenced binding value
		// For an accessor-typed binding, desc.value is JS_UNDEFINED, which is fine
		// We do not invoke getters
		JS_FreeValue(ctx, desc.getter);
		JS_FreeValue(ctx, desc.setter);
		ret = desc.value; // ownership transferred to the caller
	} else if (found < 0) {
		// A binding still in the temporal dead zone (uninitialized) throws here
		// Clear the pending exception & report unresolved
		JS_FreeValue(ctx, JS_GetException(ctx));
	}

	JS_FreeAtom(ctx, atom);
	return ret;
}

// Get/set the context's Math.random() PRNG state
//
// ctx->random_state is the entire state of the xorshift64* generator behind the Math.random intrinsic,
// so this one 64-bit value fully captures/restores the Math.random() sequence
uint64_t JS_GetRandomState(JSContext *ctx)
{
	if (!ctx) {
		return 0;
	}

	return ctx->random_state;
}

void JS_SetRandomState(JSContext *ctx, uint64_t state)
{
	if (!ctx) {
		return;
	}

	// xorshift64* must never be seeded with 0 (it would yield an all-zero stream)
	// mirror the guard in js_random_init
	ctx->random_state = state ? state : 1;
}
