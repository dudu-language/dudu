# Public Documentation Plan

Dudu's public documentation is a product surface, not a directory of repository
notes. It must let a new user install the toolchain, finish a working program,
understand the language, use native libraries, diagnose failures, and find exact
reference material without reading implementation plans.

The documentation model follows the useful parts of Rust's documentation split
and the Diataxis distinction between tutorials, how-to guides, reference, and
explanation. It does not create empty categories merely to match that model.

## Public Structure

The public site has five connected layers:

1. **Tour** explains why Dudu exists and compares concrete code with Python,
   C, C++, Rust, GLSL, and array languages.
2. **Quickstart** gets from installation to a built and tested project.
3. **Language guide** teaches syntax and semantics in the order users need them.
4. **How-to guides** cover native libraries, CMake, dependencies, tests,
   embedded targets, GPU code, and editor setup.
5. **Reference** states exact syntax, CLI behavior, built-in types, attributes,
   diagnostics, and current limitations.

The first public implementation may keep these layers in one searchable manual.
The information architecture must remain stable if sections later become
separate pages.

## Source Ownership

- `docs/appearance-spec.md` is the canonical source-language specification.
- `docs/developer-guide.md` owns compiler/project development workflows.
- `docs/import_semantics.md` owns native and Dudu import behavior.
- `docs/tests.md` owns test semantics.
- `docs/known-limitations.md` owns honest pre-alpha constraints.
- `site/docs.html` is the curated public manual, not a second independent spec.

When behavior changes, update the canonical source document and its public
summary in the same change. Public examples must compile in fixtures or be
clearly marked as illustrative.

## Quality Rules

- Put a runnable example before a long explanation.
- Use exact commands and show the expected project layout.
- Keep examples complete enough to copy.
- Link every major reference section to its canonical source.
- Separate current behavior from planned behavior.
- Do not hide C/C++ concepts that affect layout, ownership, ABI, or performance.
- Do not expose compiler implementation plans as beginner documentation.
- Include diagnostics and the correction for common failures.
- Keep navigation, anchors, and search usable without JavaScript; JavaScript may
  improve filtering and current-section tracking.
- Keep the documentation version aligned with the released toolchain version.

## Tooling Direction

The website manual is hand-curated. API documentation should be generated from
Dudu docstrings and native scanner metadata by a future `dudu doc` command. The
generated API surface should include signatures, source links, type layout when
known, examples, and imported C/C++ documentation when Clang exposes it.

Documentation validation should eventually include:

- compile and run fenced Dudu examples;
- verify internal anchors and external links;
- check CLI excerpts against `dudu --help` and `duc --help`;
- verify documented imports against real native headers;
- render desktop and mobile screenshots before publication;
- publish versioned manuals for tagged releases while keeping `master` docs
  clearly marked as development documentation.

## Completion Criteria

The alpha manual is complete when a new user can:

- install Dudu with one command;
- create, format, check, build, run, test, clean, and update a project;
- understand inference, fixed-width types, classes, enums, results, containers,
  arrays, pointers, references, allocation, generics, and compile-time values;
- import Dudu modules and C/C++ headers without guessing namespace behavior;
- add a native package through CMake or project metadata;
- configure the editor and understand diagnostics;
- find exact current limitations and the formal source specification.
