# SCL Language Reference

SCL (Structured Configuration Language) is a typed, explicit key-value configuration format.
Every document has one valid parse result. Ambiguous input is always a parse error.

---

## File structure

Every SCL file starts with a version directive, followed by field declarations.

```scl
@scl 1

name: string = "Alice"
age:  int    = 30
active: bool = true
```

`@scl 1` is required and must be the first non-comment line. The parser rejects
unknown version numbers. Fields are declared as `name: type = value`. The type
annotation is always required — omitting it is a parse error.

Field order is significant for serialization but not for semantics. Duplicate
top-level field names are a parse error.

---

## Primitive types

| Type       | Example                  | Description                              |
|------------|--------------------------|------------------------------------------|
| `string`   | `"hello"`                | UTF-8 string                             |
| `int`      | `42`, `-7`               | 64-bit signed integer                    |
| `uint`     | `42u`                    | 64-bit unsigned integer                  |
| `float`    | `3.14`, `-0.5`           | IEEE 754 double-precision                |
| `bool`     | `true`, `false`          | Boolean — only these two values          |
| `bytes`    | `b64"aGVsbG8="`          | Base64-encoded binary                    |
| `date`     | `2024-01-15`             | ISO 8601 date                            |
| `datetime` | `2024-01-15T10:00:00Z`   | ISO 8601 datetime, timezone required     |
| `duration` | `3h30m`, `90s`, `500ms`  | Human-readable duration                  |
| `null`     | `null`                   | Explicit null — only in optional fields  |

Type annotations are always required. Omitting a type is a parse error.

---

## Strings

Standard strings use double quotes with C-style escape sequences.

```scl
greeting: string = "hello world"
tab:      string = "col1\tcol2"
newline:  string = "line1\nline2"
unicode:  string = "emoji \u{1F600}"
```

Supported escapes: `\\`, `\"`, `\n`, `\r`, `\t`, `\u{HHHHHH}`.

Multiline strings use triple quotes. The leading newline is stripped. Content is
dedented based on the indentation of the closing `"""`. There is only one
multiline syntax.

```scl
query: string = """
    SELECT *
    FROM users
    WHERE active = true
"""
```

The example above produces `SELECT *\nFROM users\nWHERE active = true\n`.
Trailing newline before `"""` is preserved.

---

## Numbers

### int

64-bit signed integer. Decimal only. Negative values use a leading `-`.

```scl
port:  int = 8080
delta: int = -7
zero:  int = 0
```

### uint

64-bit unsigned integer. Written with a trailing `u` suffix.

```scl
flags: uint = 255u
max:   uint = 18446744073709551615u
```

### float

IEEE 754 double. Must include a decimal point. Exponent notation (`e`/`E`) is supported.

```scl
ratio:  float = 0.5
temp:   float = -273.15
sci:    float = 1.5e10
```

---

## Bytes

Binary data is written as a base64-encoded string with a `b64` prefix.

```scl
key:  bytes = b64"aGVsbG8="
cert: bytes = b64"MIIC..."
```

The base64 alphabet is standard RFC 4648. Padding `=` is required.

---

## Dates and times

### date

ISO 8601 date. No time component.

```scl
birthday: date = 1990-06-15
```

### datetime

ISO 8601 datetime. An explicit timezone is always required.

```scl
created: datetime = 2024-01-15T10:00:00Z
local:   datetime = 2024-01-15T10:00:00+05:30
```

UTC is written as `Z`. Offset notation (`+HH:MM`) is also valid.

With the `strict_datetime` parse option enabled, datetimes without an explicit
timezone are rejected even if the syntax is otherwise valid.

### duration

Human-readable duration built from unit suffixes: `h` (hours), `m` (minutes),
`s` (seconds), `ms` (milliseconds). Units may be combined in order from largest
to smallest.

```scl
timeout:  duration = 3h30m
interval: duration = 500ms
cooldown: duration = 1h
deadline: duration = 90s
```

---

## Optional fields

Appending `?` to a type marks the field as optional. Optional fields may be
assigned `null`. Assigning `null` to a non-optional field is a parse error.

```scl
nickname: string? = null
timeout:  int?    = 30
```

An optional field with a non-null value behaves identically to a non-optional
field. `?` is part of the type, not a default value mechanism.

---

## Lists

Lists are typed. The element type is explicit. `[any]` does not exist.

```scl
ports: [int] = [8080, 8081, 8082]

tags: [string] = [
    "production",
    "v2",
    "eu-west",
]
```

Trailing commas are always allowed. Empty lists are valid: `tags: [string] = []`.

---

## Maps

Maps have explicit key and value types.

```scl
env: {string: string} = {
    "DATABASE_URL" = "postgres://localhost/app"
    "PORT"         = "8080"
}

counts: {string: int} = {
    "errors"   = 0
    "warnings" = 3
}
```

Map keys are unique within a single map literal. Duplicate keys are a parse error.

---

## Structs

Structs are inline schema + value. Fields are declared in the type and assigned
in the value block.

```scl
server: struct {
    host: string
    port: int
    tls:  bool
} = {
    host = "localhost"
    port = 8080
    tls  = false
}
```

Structs are **closed** by default — unknown fields in the value block are a parse
error.

`struct open` allows extra fields beyond the declared schema.

```scl
metadata: struct open {
    version: string
} = {
    version = "1.0"
    author  = "team-a"
    env     = "prod"
}
```

Struct fields may be any type including nested structs, lists, and maps.

```scl
config: struct {
    server: struct { host: string  port: int }
    tags:   [string]
    env:    {string: string}
} = {
    server = { host = "localhost"  port = 8080 }
    tags   = ["api", "v2"]
    env    = { "REGION" = "eu-west" }
}
```

---

## Enums

Enums are declared at file scope with `@enum`. Variants are uppercase identifiers.

```scl
@enum LogLevel = DEBUG | INFO | WARN | ERROR

level: LogLevel = INFO
```

The enum name is used as the field type. Assigning an undeclared variant is a
parse error. Enum field values are stored as `SCL_TYPE_STRING` containing the
variant name and are accessible via `scl_value_string`.

---

## Unions

Union types combine two or more types with `|`. The value must match exactly one
of the listed types.

```scl
id:     string | int  = "user-42"
weight: float  | int  = 75
```

`null` must be listed explicitly in the union if it is a valid value.

```scl
result: string | null = null
```

Assigning `null` to a union that does not include `null` is a parse error.

---

## Constants and references

`@const` defines a reusable named value. References use `$name`.

```scl
@const defaultTimeout: int = 30

read:  int = $defaultTimeout
write: int = $defaultTimeout
```

Constants are resolved at parse time. Forward references are not allowed.
Constant names are file-scoped and are not exported through `@include`.

---

## Struct merging

Two struct values can be merged with `&`. The right side wins on conflicting keys.

```scl
@const base: struct {
    timeout: int
    retries: int
} = {
    timeout = 30
    retries = 3
}

production: struct {
    timeout: int
    retries: int
    region:  string
} = $base & {
    timeout = 60
    region  = "eu-west"
}
```

Merging is explicit. There is no hidden inheritance. Both sides must have
compatible struct types.

---

## Annotations

Annotations constrain field values and are validated at parse time. A violation
is a parse error.

```scl
port:  int      @range(1, 65535)        = 8080
name:  string   @minlen(1) @maxlen(64)  = "alice"
ratio: float    @range(0.0, 1.0)        = 0.5
email: string   @pattern("[^@]+@[^@]+") = "a@b.com"
items: [string] @minlen(1)              = ["x"]
old:   string   @deprecated             = "legacy"
```

Multiple annotations on the same field are allowed and all are checked.

| Annotation          | Applies to             | Description                              |
|---------------------|------------------------|------------------------------------------|
| `@range(min, max)`  | `int`, `uint`, `float` | Inclusive bounds — both ends included    |
| `@minlen(n)`        | `string`, `[T]`        | Minimum length / element count           |
| `@maxlen(n)`        | `string`, `[T]`        | Maximum length / element count           |
| `@pattern(regex)`   | `string`               | Must fully match the given regex         |
| `@deprecated`       | any field              | Emits a parse-time warning, not an error |

`@range` bounds are inclusive on both ends. `@pattern` anchors to the full value.
Annotations are not inherited through struct inclusion or struct merging.

---

## Includes

`@include` inserts the contents of another SCL file into the current scope.

```scl
@include "base.scl"
@include "secrets.scl" as secrets
```

Without `as`, all top-level keys from the included file are merged into the
current scope. Name collisions with already-declared fields are a parse error.

With `as name`, included keys are namespaced under `name` and accessed as a
struct field.

Circular includes are detected and reported as a parse error. The included file
must be a valid SCL document. Includes are resolved relative to the directory of
the including file. Include directives must appear after `@scl`, before any
field declarations.

---

## Comments

Only line comments are supported. Block comments do not exist.

```scl
// standalone comment

server: struct {
    // port the service listens on
    port: int // inline comment
} = {
    port = 8080
}
```

Comments are parsed and attached to the following field in the AST. They are
preserved through serialization via `scl_serialize`.

---

## Error format

All parse and type errors follow a consistent format.

```
scl error at line 4, col 12
  field "port": expected int, got string
  |  port: int = "8080"
  |              ^^^^^^
```

Every error includes: file position, field name when applicable, a description,
and a source excerpt with caret annotation. Parsing stops at the first error —
errors are not aggregated.

Warnings from `@deprecated` follow the same format but do not stop parsing. They
are returned in `scl_result_t.warnings`.

---

## File extensions

| Extension | Use                                                       |
|-----------|-----------------------------------------------------------|
| `.scl`    | Standard SCL document with type annotations and values    |
| `.scls`   | Schema-only — type definitions and annotations, no values |

---

## Non-goals

SCL is not a programming language — no loops, functions, or conditionals.
SCL is not a query language — use application code for filtering and searching.
SCL is not a binary format — use Protobuf or BARE for performance-critical serialization.
SCL is not a full schema language — use Protobuf or JSON Schema for complex contracts.
