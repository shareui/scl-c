# SCL

Structured Configuration Language: a typed, explicit configuration format with a stable C ABI library.

## What it looks like

```scl
@scl 1

host: string = "localhost"
port: int @range(1, 65535) = 8080
debug: bool = false
tags: [string] = ["production", "v2"]

db: struct {
    url: string
    pool: int
} = {
    url = "postgres://localhost/app"
    pool = 10
}
```

## Design

- Every field has an explicit type 
- Invalid input is always a parse error, never a silent guess
- One syntax, one parse result
- Zero runtime dependencies in the core library

## Docs

- [Language reference](https://github.com/shareui/scl-c/blob/main/docs/scl-lang.md)
- [C ABI reference](https://github.com/shareui/scl-c/blob/main/docs/scl-abi.md)

## Building

Requires a C11+ compiler, a C++17+ compiler, and CMake 3.16+.

```sh
cmake -B build
cmake --build build
```

Public header: `lib/include/scl.h`.

## License

See `LICENSE`.
