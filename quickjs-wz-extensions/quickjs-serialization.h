/*
 * QuickJS Serialization Extensions
 *
 * Copyright (c) 2026 Warzone 2100 Project
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
#ifndef QUICKJS_SERIALIZATION_EXTENSIONS_H
#define QUICKJS_SERIALIZATION_EXTENSIONS_H

#include <quickjs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Access to the internal state ("internal slots") of built-in objects whose contents do not live in
// own properties, so a host can save and faithfully restore them.
//
// All functions here read / write the internal state directly: they never call script-visible
// methods (Map.prototype.set, Symbol.iterator, Date.prototype.getTime, ...), so a script that
// overrides those cannot interfere, and none of them run any script code.

typedef enum JSBuiltinObjectKind
{
	JS_BUILTIN_OBJECT_KIND_OTHER = 0, // anything else (incl. plain objects, arrays, script class instances)
	JS_BUILTIN_OBJECT_KIND_MAP,
	JS_BUILTIN_OBJECT_KIND_SET,
	JS_BUILTIN_OBJECT_KIND_WEAKMAP,
	JS_BUILTIN_OBJECT_KIND_WEAKSET,
	JS_BUILTIN_OBJECT_KIND_DATE,
} JSBuiltinObjectKind;

// Classifies an object by its internal class (*not* by its prototype / constructor), so:
// - an instance of a subclass (`class Foo extends Map`) is still a Map
// - an object that merely inherits from Map.prototype (`Object.create(Map.prototype)`) is not
// - a Proxy is never classified as its target (no proxy trap is invoked)
// Non-objects return JS_BUILTIN_OBJECT_KIND_OTHER.
JSBuiltinObjectKind JS_GetBuiltinObjectKind(JSValueConst val);

// Snapshots the live entries of a Map or Set, in iteration order (which is insertion order).
//
// On success, returns 0 and sets *pentries to a newly allocated array of 2 * (*pcount) values laid out as
// [key0, value0, key1, value1, ...] (for a Set, every value is JS_UNDEFINED), each a new reference.
// The caller must release it with JS_FreeMapOrSetEntries (even if *pcount is 0; *pentries may be NULL then).
//
// Because this is a snapshot, the caller may run arbitrary code (that could mutate the Map / Set)
// while processing the entries.
//
// Returns -1 (with an exception pending) if obj is not a Map or Set, or on allocation failure.
int JS_GetMapOrSetEntries(JSContext *ctx, JSValueConst obj, JSValue **pentries, uint32_t *pcount);
void JS_FreeMapOrSetEntries(JSContext *ctx, JSValue *entries, uint32_t count);

// Creates a new, empty Map (kind == JS_BUILTIN_OBJECT_KIND_MAP) or Set (kind == JS_BUILTIN_OBJECT_KIND_SET),
// with the context's intrinsic Map.prototype / Set.prototype (the caller may JS_SetPrototype afterwards,
// ex. to restore a subclass instance)
//
// Returns JS_EXCEPTION for any other kind.
JSValue JS_NewMapOrSet(JSContext *ctx, JSBuiltinObjectKind kind);

// Adds an entry to a Map / Set, with the semantics of Map.prototype.set / Set.prototype.add (keys are normalized
// (-0 -> +0), an existing key keeps its position and (for a Map) gets the new value, a new key is appended
// at the end of the iteration order). For a Set, `value` is ignored.
//
// Returns 0 on success, or -1 (with an exception pending) if obj is not a Map or Set, or on allocation failure.
int JS_MapOrSetAdd(JSContext *ctx, JSValueConst obj, JSValueConst key, JSValueConst value);

// Gets a Date's time value (milliseconds since the epoch, or NaN for an invalid date)
//
// Returns 0 on success, or -1 (with an exception pending) if obj is not a Date.
int JS_GetDateTimeValue(JSContext *ctx, JSValueConst obj, double *pval);

// Sets a Date's time value (with the TimeClip semantics of Date.prototype.setTime: an out of range value becomes
// an invalid date (NaN), fractions are truncated)
//
// Returns 0 on success, or -1 (with an exception pending) if obj is not a Date.
int JS_SetDateTimeValue(JSContext *ctx, JSValueConst obj, double val);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_SERIALIZATION_EXTENSIONS_H */
