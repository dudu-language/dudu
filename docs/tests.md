# Dudu Tests

Dudu tests should feel closer to Cargo than Go.

Tests are explicit functions marked with `@test`:

```python
@test
def add_works():
    assert add(20, 22) == 42
```

The current implementation builds a native test harness for the selected entry
tree and runs every matching `@test` function.

## Current Commands

```sh
dudu test
dudu test src/math.dd
dudu test add_works
dudu test --filter add
```

`dudu test name` and `dudu test --filter name` use substring filtering, like
Cargo. `dudu test add` can run `add_works`, `add_more`, and any other test with
`add` in its name.

## Test Function Rules

- `@test` is only valid on free functions.
- `@test` functions take no args.
- `@test` functions return `void`, `bool`, or `i32`.
- `void` passes unless an assertion fails.
- `bool` passes when it returns `True`.
- `i32` passes when it returns zero.
- `main` is ignored when building a test harness.

## Assertions

Runtime `assert` is now a language statement:

```python
assert value > 0
```

It lowers to a hosted C++ exception with a readable failure message. This is a
language feature rather than a standard library function because it benefits
from source-location-aware lowering and test harness integration.

Python-style custom messages are supported:

```python
assert value > 0, "value must be positive"
```

## Cargo Behavior To Copy

Cargo test filtering is substring-based by default:

```sh
cargo test add
```

This runs every test whose full path contains `add`. Dudu reports zero tests
cleanly:

```text
running 0 tests
test result: ok. 0 passed; 0 failed; 0 filtered out
```

Dudu keeps generated test binaries under unique paths:

```text
build/dudu-tests/<entry-stem>-<hash>
```

The hash includes the entry path, target name, test filter, target kind, and
target mode. That avoids parallel runs stomping each other and gives us a place
to cache incremental test builds.

## Test Decorators

These stay Cargo-ish:

```python
@test
def normal_case():
    ...

@test.ignore
def slow_case():
    ...

@test.should_panic
def panics():
    ...

@test.should_panic("bad input")
def panics_with_message():
    ...
```

Ignored tests are listed but not run. `@test.should_panic` passes only when the
test throws; the string form requires the thrown message to contain that text.

## Future Test Features

Rust captures stdout by default and supports `cargo test -- --nocapture`.
Dudu should eventually support a similar mode.

Project-wide tests include:

```sh
dudu test ./...
dudu test tests/
```

`dudu test ./...` discovers testable Dudu modules recursively, similar to Go's
`go test ./...`, while keeping Cargo-style explicit test functions. Discovery
skips `build/` and `.git/` directories.

## Compile-Time Direction

The current harness emits one generated C++ file for the selected source tree.
That is simple and correct, but larger projects need incremental behavior.

The likely direction is:

- generate C++ per Dudu module where possible
- let CMake/Ninja rebuild only changed translation units
- keep hand-written C/C++ sources as normal native sources
- put generated test binaries under stable, unique build paths

This keeps Dudu in C/C++ build-system land instead of becoming a closed package
manager.

## Hosted And Freestanding

Hosted builds can support runtime `assert` directly. Freestanding and embedded
targets need an explicit policy:

- reject `assert`
- lower to a configurable panic/assert handler
- lower to a target trap

Do not silently emit hosted exception machinery in freestanding or embedded
mode.
