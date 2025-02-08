/*
 * QuickJS Limited Context Extensions
 *
 * Copyright (c) 2020 Warzone 2100 Project
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
#ifndef QUICKJS_LIMITEDCONTEXT_EXTENSIONS_H
#define QUICKJS_LIMITEDCONTEXT_EXTENSIONS_H

#include <quickjs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JSLimitedContextOptions
{
	int baseObjects;
	int dateObject;
	int eval;
	int stringNormalize; // no longer has any effect
	int regExp;
	int json;
	int proxy;
	int mapSet;
	int typedArrays;
	int promise;
	int bigInt;
	int weakRef;
} JSLimitedContextOptions;

// Constructs a context with a configurable subset of language intrinsics
JSContext *JS_NewLimitedContext(JSRuntime *rt, const JSLimitedContextOptions* options);

// Always accessible JS_Eval (even if limited context has eval disabled)
JSValue JS_Eval_BypassLimitedContext(JSContext *ctx, const char *input, size_t input_len, const char *filename, int eval_flags);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_LIMITEDCONTEXT_EXTENSIONS_H */
