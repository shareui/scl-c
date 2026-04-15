# SCL C ABI Reference

The SCL library exposes a stable C ABI. The public header is `scl.h`.
All symbols are prefixed with `scl_`. The header is valid C11 and C++17.

---

## Core types

### `scl_doc_t`

Opaque handle to a parsed or constructed document.

Owns all memory for the document tree through an internal arena allocator.
Released by `scl_doc_free`. All `scl_value_t` pointers derived from a doc
are valid only while the doc is alive — do not store them past `scl_doc_free`.

### `scl_value_t`

Opaque handle to a value node inside a document.

Lifetime is tied to the parent `scl_doc_t`. Never free a `scl_value_t` directly.

### `scl_type_t`

Enum of all value types returned by `scl_value_type`.

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

### `scl_str_t`

Heap-allocated string returned by serialization functions.

```c
typedef struct {
    char  *data;
    size_t len;
} scl_str_t;
```

`data` is null-terminated. `len` is the byte count, not including the null.
Must be freed with `scl_str_free`. Never free `data` directly.

### `scl_str_result_t`

Returned by serialization functions that can fail.

```c
typedef struct {
    bool      ok;
    scl_str_t str;
    char     *error;
} scl_str_result_t;
```

On success: `ok=true`, `str` is valid, `error` is NULL.
On failure: `ok=false`, `str` is zero, `error` is a heap-allocated string.
Free with `scl_str_result_free` regardless of outcome.

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

On success: `ok=true`, `doc` is caller-owned, `error` is NULL.
On failure: `ok=false`, `doc` is NULL, `error` is a heap-allocated string the
caller must free.

`warnings` is a heap-allocated array of heap-allocated strings. The caller
must free each string then the array, or call `scl_result_free_warnings`.
It is safe to call `scl_result_free_warnings` when `warning_count` is zero.

### `scl_parse_opts_t`

Options passed to the `_opts` parse variants. Zero-initialize for defaults.

```c
typedef struct {
    bool allow_unknown_annotations;
    bool strict_datetime;
    int  max_depth;
    bool include_root;
} scl_parse_opts_t;
```

`allow_unknown_annotations` — do not error on unrecognised annotation names.
`strict_datetime` — reject datetime values that lack an explicit timezone.
`max_depth` — maximum nesting depth; 0 means unlimited.
`include_root` — reserved, always treated as true.

---

## Versioning

```c
// compile-time constants
SCL_VERSION_MAJOR
SCL_VERSION_MINOR

// runtime query
void scl_version(int *major, int *minor);
```

Either pointer may be NULL. The ABI is stable within a major version. New
functions may be added in minor versions. Existing function signatures and
semantics do not change within a major version.

---

## Parsing

```c
scl_result_t scl_parse_str(const char *src);
scl_result_t scl_parse_file(const char *path);
scl_result_t scl_parse_str_opts(const char *src, scl_parse_opts_t opts);
scl_result_t scl_parse_file_opts(const char *path, scl_parse_opts_t opts);
```

All parse functions return `scl_result_t`. Always check `.ok` before using `.doc`.

On success the caller owns the returned `doc` and must call `scl_doc_free` exactly
once. On failure the caller must free `result.error`.

```c
scl_result_t r = scl_parse_str("@scl 1\nport: int = 8080\n");
if (!r.ok) {
    fprintf(stderr, "%s\n", r.error);
    free(r.error);
    return;
}
if (r.warning_count > 0) {
    for (size_t i = 0; i < r.warning_count; i++)
        fprintf(stderr, "warning: %s\n", r.warnings[i]);
    scl_result_free_warnings(&r);
}
// use r.doc ...
scl_doc_free(r.doc);
```

```c
void scl_result_free_warnings(scl_result_t *r);
```

Frees `r->warnings` and all strings inside it. Safe to call when
`warning_count` is zero or `r` is NULL.

---

## Document access

```c
scl_value_t *scl_get(scl_doc_t *doc, const char *key);
```

Returns the top-level value for `key`, or NULL if the key does not exist.
`doc` or `key` being NULL is safe and returns NULL.

```c
scl_value_t *scl_get_path(scl_doc_t *doc, const char *path);
```

Traverses a dot-separated path. `"server.host"` gets the `host` field of the
`server` struct. Returns NULL if any segment is missing or if an intermediate
value is not a struct or map. `doc` or `path` being NULL returns NULL.

```c
void scl_each_key(scl_doc_t *doc,
                  bool (*cb)(const char *key, scl_value_t *val, void *userdata),
                  void *userdata);
```

Calls `cb` for each top-level key in document order. Return `false` from the
callback to stop early. The `key` pointer is valid only for the duration of the
callback. Do not store it.

```c
void scl_doc_free(scl_doc_t *doc);
```

Releases all memory owned by the document including the doc struct itself.
All `scl_value_t` pointers derived from this doc become invalid after this call.
Passing NULL is safe and is a no-op.

---

## Value reading

All reader functions return `false` on type mismatch and never coerce between
types. Passing NULL for `val` is safe and always returns `false` or a zero value.
`out` may be NULL if only the type-check result is needed.

```c
scl_type_t scl_value_type(scl_value_t *val);
```

Returns the concrete type of a value. Returns `SCL_TYPE_NULL` for NULL input.

```c
bool scl_value_string(scl_value_t *val, const char **out);
```

Extracts a UTF-8 string pointer. The pointer is into doc-owned memory — do not
free it and do not store it past `scl_doc_free`. Returns `false` for
`SCL_TYPE_DATE`, `SCL_TYPE_DATETIME`, and `SCL_TYPE_DURATION` — use the
dedicated accessors for those types.

```c
bool scl_value_int(scl_value_t *val, int64_t *out);
bool scl_value_uint(scl_value_t *val, uint64_t *out);
bool scl_value_float(scl_value_t *val, double *out);
bool scl_value_bool(scl_value_t *val, bool *out);
```

Standard scalar readers. All return `false` if `val` is NULL or the wrong type.
No implicit coercion — `scl_value_float` on an `int` value returns `false`.

```c
bool scl_value_bytes(scl_value_t *val, const uint8_t **out, size_t *len);
```

Returns a pointer and byte count for a `bytes` value. For documents produced by
`scl_parse_str` or `scl_parse_file`, the base64 literal is decoded at parse
time — `out` points to raw binary and `len` is the decoded byte count. For
values built with `scl_val_bytes`, the data is stored as provided.

```c
bool scl_value_date(scl_value_t *val, const char **out);
bool scl_value_datetime(scl_value_t *val, const char **out);
bool scl_value_duration(scl_value_t *val, const char **out);
```

Return the raw textual form as stored in the document (e.g. `"2024-01-15"`,
`"2024-01-15T10:00:00Z"`, `"3h30m"`). The pointer is doc-owned — do not free
or store past `scl_doc_free`.

```c
bool scl_value_is_null(scl_value_t *val);
```

Returns `true` for both NULL input and `SCL_TYPE_NULL` values.

### List access

```c
size_t       scl_list_len(scl_value_t *val);
scl_value_t *scl_list_get(scl_value_t *val, size_t index);
```

`scl_list_len` returns 0 for non-list values or NULL input.
`scl_list_get` returns NULL for out-of-bounds access or NULL input.

### Struct and map access

```c
scl_value_t *scl_struct_get(scl_value_t *val, const char *key);
void         scl_struct_each(scl_value_t *val,
                             bool (*cb)(const char *key, scl_value_t *v, void *userdata),
                             void *userdata);
```

Both work on `SCL_TYPE_STRUCT` and `SCL_TYPE_MAP`. They are no-ops / return NULL
for other types and for NULL input.

`scl_struct_each` calls `cb` for each key in insertion order. Return `false`
from the callback to stop early. The `key` pointer is valid only for the duration
of the callback.

---

## Building documents

Documents can be constructed programmatically without parsing. All values are
allocated inside the doc's arena and are freed automatically by `scl_doc_free`.

```c
scl_doc_t *scl_doc_new(void);
```

Creates an empty document. The caller owns it and must call `scl_doc_free`.

### Value constructors

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

All constructors allocate inside `doc`'s arena. The returned pointer is valid
until `scl_doc_free`.

`scl_val_string` copies the string into the arena. The caller does not need to
keep `s` alive.

`scl_val_bytes` copies `len` bytes from `data` into the arena. The data is
stored as-is — no base64 encoding is applied. If you want to round-trip through
the SCL serializer, encode the data yourself before calling this function.

`scl_val_list_new` creates an empty list. Elements are added with
`scl_doc_list_push`.

`scl_val_struct_new` creates an empty struct. Fields are added with
`scl_doc_struct_set`.

### Mutation

```c
void scl_doc_set(scl_doc_t *doc, const char *key, scl_value_t *val);
```

Sets a top-level field on the document. If `key` already exists, the value is
replaced. The key string is copied into the arena.

```c
void scl_doc_struct_set(scl_doc_t *doc, scl_value_t *strct,
                        const char *key, scl_value_t *val);
```

Sets a named field on a struct value. If the key already exists, the value is
replaced. The key string is copied into the arena.

```c
void scl_doc_list_push(scl_doc_t *doc, scl_value_t *list, scl_value_t *val);
```

Appends a value to a list. The list grows as needed using the doc arena.

### Legacy variants

```c
void scl_list_push(scl_value_t *list, scl_value_t *val);
void scl_struct_set(scl_value_t *strct, const char *key, scl_value_t *val);
```

These variants have no access to the doc arena. Growth buffers are allocated
via `malloc` and tracked for release by `scl_doc_free`. They are safe to use
but prefer the `scl_doc_*` variants for new code.

In `scl_struct_set`, `key` must remain valid until `scl_doc_free` — it is not
copied. In the `scl_doc_struct_set` variant the key is always copied.

### Example: building a document

```c
scl_doc_t *doc = scl_doc_new();

// flat primitives
scl_doc_set(doc, "host", scl_val_string(doc, "localhost"));
scl_doc_set(doc, "port", scl_val_int(doc, 8080));
scl_doc_set(doc, "tls",  scl_val_bool(doc, false));

// list
scl_value_t *tags = scl_val_list_new(doc);
scl_doc_list_push(doc, tags, scl_val_string(doc, "api"));
scl_doc_list_push(doc, tags, scl_val_string(doc, "v2"));
scl_doc_set(doc, "tags", tags);

// nested struct
scl_value_t *db = scl_val_struct_new(doc);
scl_doc_struct_set(doc, db, "url",  scl_val_string(doc, "postgres://localhost/app"));
scl_doc_struct_set(doc, db, "pool", scl_val_int(doc, 10));
scl_doc_set(doc, "db", db);

// serialize
scl_str_t out = scl_serialize(doc);
printf("%.*s\n", (int)out.len, out.data);
scl_str_free(out);

scl_doc_free(doc);
```

### Example: reading a value safely

```c
scl_value_t *v = scl_get(doc, "port");
if (v == NULL) {
    // key does not exist
    return;
}
int64_t port = 0;
if (!scl_value_int(v, &port)) {
    // wrong type
    return;
}
printf("port: %lld\n", (long long)port);
```

### Example: iterating struct fields

```c
typedef struct { int total; } CountCtx;

static bool countCb(const char *key, scl_value_t *val, void *ud) {
    CountCtx *ctx = ud;
    ctx->total++;
    (void)key; (void)val;
    return true;
}

CountCtx ctx = {0};
scl_value_t *srv = scl_get(doc, "server");
scl_struct_each(srv, countCb, &ctx);
printf("server has %d fields\n", ctx.total);
```

### Example: walking a document

```c
static bool printKey(const char *key, scl_value_t *val, void *ud) {
    int depth = *(int *)ud;
    scl_type_t t = scl_value_type(val);
    printf("%*s%s (%d)\n", depth * 2, "", key, (int)t);
    if (t == SCL_TYPE_STRUCT || t == SCL_TYPE_MAP) {
        int next = depth + 1;
        scl_struct_each(val, printKey, &next);
    }
    return true;
}

int depth = 0;
scl_each_key(doc, printKey, &depth);
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

`scl_serialize` serializes back to SCL text. Always succeeds. Returns an empty
string for a NULL doc.

`scl_to_json` serializes to JSON. Always succeeds. Type mapping:

| SCL type    | JSON representation              |
|-------------|----------------------------------|
| `string`    | JSON string                      |
| `int`       | JSON number                      |
| `uint`      | JSON number                      |
| `float`     | JSON number                      |
| `bool`      | `true` / `false`                 |
| `null`      | `null`                           |
| `bytes`     | JSON string (raw base64 text)    |
| `date`      | JSON string (ISO 8601)           |
| `datetime`  | JSON string (ISO 8601)           |
| `duration`  | JSON string (e.g. `"3h30m"`)    |
| `list`      | JSON array                       |
| `struct`    | JSON object                      |
| `map`       | JSON object                      |

`scl_to_toml` serializes to TOML. Fails with an error if the document contains
`bytes` or `union` values — TOML has no equivalent types. Always check `.ok`
before using the result.

Always free serialization results with `scl_str_free` or `scl_str_result_free`.

```c
scl_str_result_t r = scl_to_toml(doc);
if (!r.ok) {
    fprintf(stderr, "toml error: %s\n", r.error);
    scl_str_result_free(&r);
    return;
}
printf("%.*s\n", (int)r.str.len, r.str.data);
scl_str_result_free(&r);
```

---

## Memory ownership summary

| What                         | Who owns it | How to free                    |
|------------------------------|-------------|--------------------------------|
| `scl_result_t.doc`           | caller      | `scl_doc_free`                 |
| `scl_result_t.error`         | caller      | `free`                         |
| `scl_result_t.warnings`      | caller      | `scl_result_free_warnings`     |
| `scl_value_t *` from doc     | doc         | freed by `scl_doc_free`        |
| strings from value readers   | doc         | freed by `scl_doc_free`        |
| `scl_str_t` from serializers | caller      | `scl_str_free`                 |
| `scl_str_result_t`           | caller      | `scl_str_result_free`          |

Key rule: anything returned from a value reader (strings, byte pointers) is
doc-owned — do not free it and do not store it past `scl_doc_free`.

---

## Notes on bytes

`scl_value_bytes` returns decoded binary data. The parser decodes the base64
literal at parse time — `b64"aGVsbG8="` yields 5 bytes `{0x68,0x65,0x6c,0x6c,0x6f}`
(`"hello"`). `len` is the decoded byte count.

`scl_val_bytes` (builder API) stores raw binary as-is — no base64 encoding is
applied. The two paths are symmetric: both store and return raw bytes.

If you need to serialize a built document back to SCL and round-trip through
`scl_parse_str`, encode the binary data with base64 before calling
`scl_val_bytes` so the serialized text is a valid `b64"..."` literal.

---

## Thread safety

Parsing is stateless — multiple threads may call `scl_parse_str` or
`scl_parse_file` concurrently on different inputs without locking.

A single `scl_doc_t` is not thread-safe. Do not read from and write to the same
document concurrently. Multiple threads may read from the same document if no
thread is modifying it.

---

## ABI stability

The ABI is stable within a major version. Minor versions may add new functions.
Existing function signatures, return types, and documented semantics do not
change within a major version.

Symbol visibility is controlled by `SCL_API`. On shared library builds, only
`SCL_API`-tagged symbols are exported. Internal symbols are hidden.
