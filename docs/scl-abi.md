# SCL C ABI Reference

The SCL library exposes a stable C ABI. The public header is `scl.h`.  
All symbols are prefixed with `scl_`. The header is valid C11 and C++17.

---

## Core types

### `scl_doc_t`

Opaque handle to a parsed or constructed document.  
Owns all memory for the document tree. Released by `scl_doc_free`.  
`scl_value_t` pointers derived from a doc are valid only while the doc is alive.

### `scl_value_t`

Opaque handle to a value node inside a document.  
Lifetime is tied to the parent `scl_doc_t`. Do not store past `scl_doc_free`.

### `scl_type_t`

Enum of all value types.

```c
typedef enum {
    SCL_TYPE_NULL,
    SCL_TYPE_STRING,
    SCL_TYPE_INT,
    SCL_TYPE_UINT,
    SCL_TYPE_FLOAT,
    SCL_TYPE_BOOL,
    SCL_TYPE_BYTES,
    SCL_TYPE_DATE,
    SCL_TYPE_DATETIME,
    SCL_TYPE_DURATION,
    SCL_TYPE_LIST,
    SCL_TYPE_MAP,
    SCL_TYPE_STRUCT,
    SCL_TYPE_UNION
} scl_type_t;
```

### `scl_result_t`

Returned by all parse functions.

```c
typedef struct {
    bool        ok;
    scl_doc_t  *doc;
    char       *error;
    char      **warnings;
    size_t      warning_count;
} scl_result_t;
```

On success: `ok = true`, `doc` is caller-owned, `error` is NULL.  
On failure: `ok = false`, `doc` is NULL, `error` is a heap-allocated string the caller must free.  
`warnings` is a heap-allocated array of heap-allocated strings. Free each string then the array, or call `scl_result_free_warnings`.

### `scl_str_t`

Heap-allocated string returned by serialization functions. Must be freed with `scl_str_free`.

```c
typedef struct {
    char  *data;
    size_t len;
} scl_str_t;
```

### `scl_str_result_t`

Returned by serialization functions that can fail (e.g. `scl_to_toml`).

```c
typedef struct {
    bool      ok;
    scl_str_t str;
    char     *error;
} scl_str_result_t;
```

Free with `scl_str_result_free`.

### `scl_parse_opts_t`

Options for parse variants. Zero-initialize for defaults.

```c
typedef struct {
    bool allow_unknown_annotations;
    bool strict_datetime;
    int  max_depth;
    bool include_root;
} scl_parse_opts_t;
```

`allow_unknown_annotations` — do not error on unrecognised annotation names.  
`strict_datetime` — reject datetime values without an explicit timezone.  
`max_depth` — maximum nesting depth; 0 means unlimited.  
`include_root` — reserved.

---

## Parsing

```c
scl_result_t scl_parse_str(const char *src);
scl_result_t scl_parse_file(const char *path);
scl_result_t scl_parse_str_opts(const char *src, scl_parse_opts_t opts);
scl_result_t scl_parse_file_opts(const char *path, scl_parse_opts_t opts);
```

All parse functions return `scl_result_t`. Always check `.ok` before using `.doc`.  
On success the caller owns the returned `doc` and must call `scl_doc_free` exactly once.  
On failure the caller must free `result.error`.

```c
scl_result_t r = scl_parse_str("@scl 1\nport: int = 8080\n");
if (!r.ok) {
    fprintf(stderr, "%s\n", r.error);
    free(r.error);
    return;
}
scl_result_free_warnings(&r);
// use r.doc
scl_doc_free(r.doc);
```

```c
void scl_result_free_warnings(scl_result_t *r);
```

Frees `r->warnings` and all strings inside it. Safe to call when `warning_count` is zero.

---

## Document access

```c
scl_value_t *scl_get(scl_doc_t *doc, const char *key);
```

Returns the top-level value for `key`, or NULL if the key does not exist.

```c
scl_value_t *scl_get_path(scl_doc_t *doc, const char *path);
```

Traverses a dot-separated path. `"server.host"` is equivalent to getting `server` then `host`.  
Returns NULL if any segment is missing or if an intermediate value is not a struct or map.

```c
void scl_each_key(scl_doc_t *doc,
                  bool (*cb)(const char *key, scl_value_t *val, void *userdata),
                  void *userdata);
```

Calls `cb` for each top-level key in document order. Return `false` from the callback to stop early.

```c
void scl_doc_free(scl_doc_t *doc);
```

Releases all memory owned by the document including the doc struct itself.  
All `scl_value_t` pointers derived from this doc become invalid after this call.

---

## Value reading

All reader functions return `false` on type mismatch and never coerce between types.  
Passing NULL for `val` is safe and always returns `false` or a zero value.

```c
scl_type_t scl_value_type(scl_value_t *val);
```

Returns the concrete type of a value. Returns `SCL_TYPE_NULL` for NULL input.

```c
bool scl_value_string(scl_value_t *val, const char **out);
bool scl_value_int(scl_value_t *val, int64_t *out);
bool scl_value_uint(scl_value_t *val, uint64_t *out);
bool scl_value_float(scl_value_t *val, double *out);
bool scl_value_bool(scl_value_t *val, bool *out);
bool scl_value_bytes(scl_value_t *val, const uint8_t **out, size_t *len);
bool scl_value_date(scl_value_t *val, const char **out);
bool scl_value_datetime(scl_value_t *val, const char **out);
bool scl_value_duration(scl_value_t *val, const char **out);
bool scl_value_is_null(scl_value_t *val);
```

String, date, datetime, and duration return a pointer into doc-owned memory. Do not free or store past `scl_doc_free`.  
`scl_value_string` returns `false` for `SCL_TYPE_DATE`, `SCL_TYPE_DATETIME`, and `SCL_TYPE_DURATION` — use the specific accessor.  
`scl_value_is_null` returns `true` for both NULL input and `SCL_TYPE_NULL` values.  
`out` may be NULL if only the type check result is needed.

```c
size_t       scl_list_len(scl_value_t *val);
scl_value_t *scl_list_get(scl_value_t *val, size_t index);
```

`scl_list_len` returns 0 for non-list values.  
`scl_list_get` returns NULL for out-of-bounds access.

```c
scl_value_t *scl_struct_get(scl_value_t *val, const char *key);
void         scl_struct_each(scl_value_t *val,
                             bool (*cb)(const char *key, scl_value_t *v, void *userdata),
                             void *userdata);
```

Both work on `SCL_TYPE_STRUCT` and `SCL_TYPE_MAP`. Return NULL / no-op for other types.

---

## Building documents

Documents can be constructed programmatically without parsing.

```c
scl_doc_t *scl_doc_new(void);
```

Creates an empty document. The caller owns it and must call `scl_doc_free`.

```c
scl_value_t *scl_val_string(scl_doc_t *doc, const char *s);
scl_value_t *scl_val_int(scl_doc_t *doc, int64_t v);
scl_value_t *scl_val_uint(scl_doc_t *doc, uint64_t v);
scl_value_t *scl_val_float(scl_doc_t *doc, double v);
scl_value_t *scl_val_bool(scl_doc_t *doc, bool v);
scl_value_t *scl_val_null(scl_doc_t *doc);
scl_value_t *scl_val_bytes(scl_doc_t *doc, const uint8_t *data, size_t len);
scl_value_t *scl_val_list_new(scl_doc_t *doc);
scl_value_t *scl_val_struct_new(scl_doc_t *doc);
```

All value constructors allocate inside `doc`'s arena. The returned pointer is valid until `scl_doc_free`.

```c
void scl_doc_list_push(scl_doc_t *doc, scl_value_t *list, scl_value_t *val);
void scl_doc_struct_set(scl_doc_t *doc, scl_value_t *strct, const char *key, scl_value_t *val);
void scl_doc_set(scl_doc_t *doc, const char *key, scl_value_t *val);
```

These are the preferred builder functions — they allocate entirely within the doc arena.  
`scl_doc_set` sets a top-level field. `scl_doc_struct_set` sets a field on a nested struct value.  
The `key` string is copied into the arena; the caller does not need to keep it alive.

```c
// legacy variants — kept for ABI compatibility
void scl_list_push(scl_value_t *list, scl_value_t *val);
void scl_struct_set(scl_value_t *strct, const char *key, scl_value_t *val);
```

These variants have no access to the doc arena and allocate growth buffers via `malloc`.  
Those buffers are tracked and freed on `scl_doc_free`. They are safe to use but prefer the `scl_doc_*` variants for new code.  
`key` in `scl_struct_set` must remain valid until `scl_doc_free` — it is not copied.

Example:

```c
scl_doc_t *doc = scl_doc_new();

scl_value_t *list = scl_val_list_new(doc);
scl_doc_list_push(doc, list, scl_val_int(doc, 1));
scl_doc_list_push(doc, list, scl_val_int(doc, 2));

scl_value_t *server = scl_val_struct_new(doc);
scl_doc_struct_set(doc, server, "host", scl_val_string(doc, "localhost"));
scl_doc_struct_set(doc, server, "port", scl_val_int(doc, 8080));

scl_doc_set(doc, "ports", list);
scl_doc_set(doc, "server", server);

scl_str_t out = scl_serialize(doc);
printf("%.*s\n", (int)out.len, out.data);
scl_str_free(out);

scl_doc_free(doc);
```

---

## Serialization

```c
scl_str_t        scl_serialize(scl_doc_t *doc);
scl_str_t        scl_to_json(scl_doc_t *doc);
scl_str_result_t scl_to_toml(scl_doc_t *doc);
void             scl_str_free(scl_str_t s);
void             scl_str_result_free(scl_str_result_t *r);
```

`scl_serialize` and `scl_to_json` always succeed. They return an empty string for a NULL doc.  
`scl_to_toml` fails if the document contains `bytes` or `union` values — check `.ok` before use.  
Always free the result with `scl_str_free` or `scl_str_result_free`.

---

## Memory ownership summary

| What | Who owns it | How to free |
|---|---|---|
| `scl_result_t.doc` | caller | `scl_doc_free` |
| `scl_result_t.error` | caller | `free` |
| `scl_result_t.warnings` | caller | `scl_result_free_warnings` |
| `scl_value_t *` from doc | doc | freed by `scl_doc_free` |
| strings from value readers | doc | freed by `scl_doc_free` |
| `scl_str_t` from serialization | caller | `scl_str_free` |
| `scl_str_result_t` | caller | `scl_str_result_free` |

---

## Versioning

The library version is available at compile time and runtime.

```c
// compile time
SCL_VERSION_MAJOR
SCL_VERSION_MINOR

// runtime
void scl_version(int *major, int *minor);
```

The ABI is stable within a major version. New functions may be added in minor versions.  
Existing function signatures and semantics do not change within a major version.
