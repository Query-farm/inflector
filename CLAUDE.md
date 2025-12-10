# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is **Inflector**, a DuckDB extension that provides string case transformation and inflection capabilities (snake_case, camelCase, pluralization, etc.). It's a DuckDB Community Extension developed by Query.Farm.

## Build Commands

```bash
# Build release version
make release

# Build debug version
make debug

# Run tests (uses SQLLogicTests)
make test           # runs release tests
make test_debug     # runs debug tests

# Format code
make format

# Update DuckDB submodule
make update
```

All extension functions should be documented inside of DuckDB with CreateScalarFunctionInfo or CreateAggregateFunctionInfo or the appropriate type for the function.  This documentation of the function should include examples, parameter types and parameter names.  The function should be categorized.


## Architecture

### Hybrid C++/Rust Design

The extension uses a hybrid architecture:
- **C++ layer** (`src/`): DuckDB extension interface, function registration, type handling
- **Rust layer** (`duckdb_inflector_binding/`): String transformation logic using the `cruet` crate

The Rust code compiles to a static library that's linked into the DuckDB extension via CMake + Corrosion.

### Key Files

- `src/inflector_extension.cpp` - Main extension entry point, registers all SQL functions
- `duckdb_inflector_binding/src/lib.rs` - Rust FFI bindings wrapping the `cruet` crate
- `CMakeLists.txt` - Build configuration including Rust/Corrosion setup
- `extension_config.cmake` - DuckDB extension loader configuration

### Function Registration Pattern

The C++ code uses helper functions for registering SQL functions:
- `RegisterInflectorTransform()` - Registers string transformation functions (returns VARCHAR)
- `RegisterInflectorPredicate()` - Registers boolean predicate functions (returns BOOLEAN)

The `inflect()` function has overloads for:
1. Table function - transforms column names in query results
2. Scalar function with VARCHAR - transforms string values
3. Scalar function with STRUCT/ANY - transforms struct field names recursively

### Testing

Tests are SQLLogicTests in `test/sql/`. Run with `make test` or `make test_debug`.

### Cross-Platform Rust Targets

`CMakeLists.txt` handles Rust target selection for various platforms (Linux glibc/musl, macOS arm64/x86_64, Windows MSVC/MinGW, WASM).
