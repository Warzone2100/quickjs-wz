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

// NOTE: This file is #included directly at the end of quickjs.c (see patches/001-add-extensions.patch),
// so it can access the engine internals (JSMapState / JSMapRecord / JSObject, and static helpers).

#include <quickjs-serialization.h>

// The internal Date time value accessors (QuickJS renamed them, QuickJS-NG keeps the older names)
#if defined(QUICKJS_NG)
# define JS_WZ_GetDateValueInternal JS_ThisTimeValue
# define JS_WZ_SetDateValueInternal JS_SetThisTimeValue
#else
# define JS_WZ_GetDateValueInternal JS_ThisDateValue
# define JS_WZ_SetDateValueInternal JS_SetThisDateValue
#endif

// (JS_GetClassID takes a non-const JSValue in QuickJS)
static JSClassID js_wz_get_class_id(JSValueConst val)
{
	if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
		return 0;
	return JS_VALUE_GET_OBJ(val)->class_id;
}

JSBuiltinObjectKind JS_GetBuiltinObjectKind(JSValueConst val)
{
	switch (js_wz_get_class_id(val)) {
	case JS_CLASS_MAP: return JS_BUILTIN_OBJECT_KIND_MAP;
	case JS_CLASS_SET: return JS_BUILTIN_OBJECT_KIND_SET;
	case JS_CLASS_WEAKMAP: return JS_BUILTIN_OBJECT_KIND_WEAKMAP;
	case JS_CLASS_WEAKSET: return JS_BUILTIN_OBJECT_KIND_WEAKSET;
	case JS_CLASS_DATE: return JS_BUILTIN_OBJECT_KIND_DATE;
	default: return JS_BUILTIN_OBJECT_KIND_OTHER;
	}
}

// Returns the JSMapState of a (non-weak) Map or Set, or NULL (with an exception pending)
static JSMapState *js_wz_get_map_or_set_state(JSContext *ctx, JSValueConst obj, int *pmagic)
{
	JSClassID class_id = js_wz_get_class_id(obj);
	JSMapState *s;
	if (class_id != JS_CLASS_MAP && class_id != JS_CLASS_SET) {
		JS_ThrowTypeError(ctx, "not a Map or Set");
		return NULL;
	}
	s = (JSMapState *)JS_GetOpaque(obj, class_id);
	if (!s) {
		JS_ThrowTypeError(ctx, "not a Map or Set");
		return NULL;
	}
	if (pmagic) {
		*pmagic = (int)(class_id - JS_CLASS_MAP); // 0 = Map, MAGIC_SET = Set (see js_map_constructor)
	}
	return s;
}

int JS_GetMapOrSetEntries(JSContext *ctx, JSValueConst obj, JSValue **pentries, uint32_t *pcount)
{
	JSMapState *s;
	struct list_head *el;
	JSMapRecord *mr;
	JSValue *entries;
	uint32_t count = 0, i = 0;

	*pentries = NULL;
	*pcount = 0;

	s = js_wz_get_map_or_set_state(ctx, obj, NULL);
	if (!s)
		return -1;

	// The records list is in iteration order. It can also contain "empty" (zombie) records:
	// deleted entries kept allocated while an iterator still points at them - skip those.
	list_for_each(el, &s->records) {
		mr = list_entry(el, JSMapRecord, link);
		if (!mr->empty)
			count++;
	}
	if (count == 0)
		return 0;

	entries = (JSValue *)js_malloc(ctx, sizeof(JSValue) * 2 * (size_t)count);
	if (!entries)
		return -1;

	// Nothing in this loop can run script code or mutate the map
	list_for_each(el, &s->records) {
		mr = list_entry(el, JSMapRecord, link);
		if (mr->empty)
			continue;
		entries[2 * i] = JS_DupValue(ctx, mr->key);
		entries[2 * i + 1] = JS_DupValue(ctx, mr->value);
		i++;
	}

	*pentries = entries;
	*pcount = count;
	return 0;
}

void JS_FreeMapOrSetEntries(JSContext *ctx, JSValue *entries, uint32_t count)
{
	uint32_t i;
	if (!entries)
		return;
	for (i = 0; i < 2 * count; i++) {
		JS_FreeValue(ctx, entries[i]);
	}
	js_free(ctx, entries);
}

JSValue JS_NewMapOrSet(JSContext *ctx, JSBuiltinObjectKind kind)
{
	int magic;
	switch (kind) {
	case JS_BUILTIN_OBJECT_KIND_MAP: magic = 0; break;
	case JS_BUILTIN_OBJECT_KIND_SET: magic = MAGIC_SET; break;
	default:
		return JS_ThrowTypeError(ctx, "not a Map or Set kind");
	}
	// An undefined new_target makes the object creation use the intrinsic prototype (ctx->class_proto),
	// and without an iterable argument the constructor does not call any (script-visible) adder
	return js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, magic);
}

int JS_MapOrSetAdd(JSContext *ctx, JSValueConst obj, JSValueConst key, JSValueConst value)
{
	int magic = 0;
	JSValueConst args[2];
	JSValue ret;

	if (!js_wz_get_map_or_set_state(ctx, obj, &magic))
		return -1;

	// Calls the C implementation directly (not the script-visible, possibly overridden, method)
	args[0] = key;
	args[1] = value;
	ret = js_map_set(ctx, obj, 2, args, magic);
	if (JS_IsException(ret))
		return -1;
	JS_FreeValue(ctx, ret);
	return 0;
}

int JS_GetDateTimeValue(JSContext *ctx, JSValueConst obj, double *pval)
{
	return JS_WZ_GetDateValueInternal(ctx, pval, obj);
}

int JS_SetDateTimeValue(JSContext *ctx, JSValueConst obj, double val)
{
	JSValue ret = JS_WZ_SetDateValueInternal(ctx, obj, time_clip(val));
	if (JS_IsException(ret))
		return -1;
	JS_FreeValue(ctx, ret);
	return 0;
}
