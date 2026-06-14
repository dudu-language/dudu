# C++ Stdlib Interop Goal

Dudu should be able to consume ordinary C and C++ standard library headers directly. The goal is not to model every corner of C++ template metaprogramming. The goal is that common STL APIs feel usable from Dudu without wrapper headers, `cpp()` escape hatches, or predeclared native types.

The important proof is compile-and-run coverage. Shape-only tests are useful, but stdlib interop is only convincing when Dudu emits C++ that compiles against the platform C++ standard library and behaves correctly.

## Current Coverage

- `std::vector`, `std::string`, and `std::unordered_map`
- `std::unique_ptr`, `std::make_unique`, and `std::move`
- Explicit native function templates
- Explicit native member templates
- `std::filesystem::path`
- `std::chrono`
- `std::thread`, `std::atomic`, and `std::ref`
- `std::optional`
- `std::variant`
- `std::array`
- `std::span`
- `std::string_view`
- `std::map`
- `std::unordered_set`
- `std::shared_ptr` and `std::weak_ptr`
- Scanner-derived template method returns, including `weak_ptr.lock`
- `std::mutex` and `std::lock_guard`
- `std::function`
- `std::sort`
- `std::count`
- `std::find`
- `std::lower_bound`
- `std::remove` plus container `erase`
- `std::set`, `std::deque`, `std::priority_queue`
- `std::pair`, `std::tuple`, `std::make_pair`, `std::make_tuple`, and
  templated `std::get`
- Dudu-side diagnostics for stdlib callback argument mismatches
- Dudu surface containers lowering to STL containers:
  - `list[T]` -> `std::vector<T>`
  - `dict[K, V]` -> `std::unordered_map<K, V>`
  - `set[T]` -> `std::unordered_set<T>`
  - `Option[T]` -> `std::optional<T>`
  - `atomic[T]` -> `std::atomic<T>`

## Interop Boundary

Dudu may define native forms that lower to C++ library types, such as `list[T]`,
`dict[K, V]`, `set[T]`, `Option[T]`, `atomic[T]`, `str`, and `cstr`.
Imported C++ code should not get name-specific compiler behavior. If a program
uses `std.vector[T]`, `std.thread`, `std.function[...]`, `std.shared_ptr[T]`, or
any other imported type directly, semantic information should come from native
header scanning or from a generic C++ interop rule.

Current generic C++ interop rules include template type lowering, namespace
qualification for templated return types, dependent receiver-template return
substitution, callable `fn(...)` template arguments, and positional native
constructor fallback when scanner metadata is incomplete.

## Executed Fixtures

- `tests/fixtures/cpp_stdlib_interop.dd`
- `tests/fixtures/cpp_stdlib_algorithms.dd`
- `tests/fixtures/cpp_std_variant.dd`
- `tests/fixtures/bad_cpp_std_function_call.dd`

These fixtures are wired into `scripts/test.sh` and `scripts/test_negative.sh`.

## Future Hardening

- More algorithm/range APIs when they represent real library usage.
- More overload diagnostic fixtures for large real-world overload sets.
- More scanner coverage for STL member methods that are not exposed by platform system-header AST dumps.

## Acceptance

- Add fixtures that compile and run through `scripts/test.sh`.
- Prefer one or a few dense fixtures over many tiny emit-only checks.
- Keep wrapper headers out of these tests unless the test is explicitly documenting an unsupported C++ pattern.
- If an STL feature exposes a Dudu gap, either fix it or leave a focused failing/negative fixture and document the missing capability here.
