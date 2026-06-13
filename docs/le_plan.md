# Le Plan

This is the next practical roadmap for Dudu.

Dudu is far enough along to guide development by making real examples easier
instead of planning the whole language in the abstract.

## 1. Finish The Project Driver

Primary plan: [Dudu Project Driver Plan](project-driver-plan.md).

Make daily use feel good:

- named targets: done
- `dudu clean`: done
- clean Cargo-ish build logs: done
- `dudu test ./...`: done
- better test binary output paths: done
- examples and docs that prefer `dudu run` where it improves usability: done

This pays off immediately because every future example and feature becomes
easier to build, run, and test.

## 2. Constructors, Destructors, And Operators

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Make the C++ object model feel complete through Python-shaped syntax:

- constructor behavior
- destructor behavior
- overloads
- operator overloads
- member methods
- static methods
- C++ interop behavior for all of the above

This is more important than user-defined macros because it directly affects
normal systems and game code.

## 3. Static Members And Namespaced Constants

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Support class-scoped constants, static data, and static functions.

Status: class-scoped constants and `@staticmethod` methods are implemented.
Mutable static data and broader namespaced constants remain candidates if real
examples need them.

Python, Rust, C, and C++ all have ways to hang values off a type or namespace.
Dudu needs that so constants and helpers do not all live in the global module
scope.

Target shape:

```python
class Color:
    WHITE: Color = Color(1.0, 1.0, 1.0, 1.0)

    r: f32
    g: f32
    b: f32
    a: f32

class Math:
    PI: f64 = 3.141592653589793

    @static
    def clamp(x: f32, lo: f32, hi: f32) -> f32:
        if x < lo:
            return lo
        if x > hi:
            return hi
        return x
```

## 4. Harden Native Header Awareness

Primary plan: [Native Header Awareness Plan](header-awareness-plan.md).

The C/C++ interop promise depends on imported headers feeling reliable.

Drive this with real library stress tests:

- raylib
- SDL3
- ImGui
- glm
- sqlite
- POSIX
- C++ standard library containers and utilities

Important areas:

- macros
- templates
- overloads
- namespaces
- inherited C++ classes
- diagnostics when Clang tooling or include paths are wrong

## 5. Polish The Test System

Primary plan: [Dudu Tests](tests.md).

Keep it Cargo-ish.

Short-term fixes:

- `0 tests` output instead of `0/0 tests passed`
- unique test binary paths: done
- `dudu test ./...`: done
- `assert expr, "message"`

Longer-term features:

- `@test.ignore`
- `@test.should_panic`
- stdout capture
- no-capture mode

## 6. Keep Macros And Decorators Conservative

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Do not build a full user-defined macro system yet.

Do add compiler-recognized decorators when they remove real friction:

- `@test`
- `@inline`
- `@extern_c`
- `@static`
- `@operator`
- target attributes such as `@cuda.global`

User-defined decorators, hygienic macros, and lisp-style compile-time
metaprogramming are separate language design work and should not block the
core C/C++ replacement layer.

## 7. Avoid Native Dudu Inheritance For Now

Related docs:

- [Native Header Awareness Plan](header-awareness-plan.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Do not add Dudu-native inheritance unless real examples force it.

For C++ interop, Dudu still needs to consume inherited C++ classes well enough
to:

- call inherited methods
- construct C++ types that use inheritance
- pass derived/base pointers and references correctly

That is an interop requirement, not a reason to design inheritance into Dudu's
own object model immediately.
