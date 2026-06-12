## File Size

- Target file size is roughly 300-500 lines.
- If a file is clearly growing past that range, prefer splitting it.
- Split by cohesive responsibility, not arbitrarily.
- Small exceptions are fine when the language or framework strongly favors a
  single file, or when a split would make navigation worse.

## Code Organization

- Group code by functionality.
- Keep related functions near each other.
- Prefer files that have one clear job.
- Prefer mostly flat module structure.
- Avoid deep module nesting unless there is a strong, concrete reason for it.
- Small groups are fine where they clearly improve ownership or navigation.
- Prefer modules organized around behavior and ownership, not around vague
  categories like `utils` unless the helpers are truly shared and generic.

## Style

- Default style target is "C+" / "C+-" code:
  - direct
  - explicit
  - readable
  - low-magic
  - modest abstraction
  - easy to trace in a debugger
- Prefer straightforward control flow over clever indirection.
- Avoid deep nesting in `if` statements and general logic.
- Prefer early returns, helper functions, and flatter branching when that keeps
  behavior clearer.
- Prefer obvious data movement over framework tricks.
- Prefer small helper functions over deep abstraction stacks.
- Avoid unnecessary genericization.
- Avoid premature reuse that makes local behavior harder to understand.
- Keep naming concrete and descriptive.

## Language Pragmatism

- Dudu is intended to be C/C++-ish with lighter syntax, not Python semantics.
- Prefer explicit compile-time behavior over runtime magic.
- Keep C and C++ interoperability central to design decisions.
- Do not add Rust-style ownership or lifetime rules unless the project goals
  explicitly change.

## Change Discipline

- Preserve documented language behavior unless the task is to change it.
- Do not perform broad style rewrites unless requested.
- When splitting files, keep the resulting layout obvious and boring.
- Commit frequently at reasonable green checkpoints.
- Keep commits scoped to one feature, diagnostic, doc update, or test slice.
- Run the relevant formatter and tests before committing.

## When in Doubt

- Choose the more explicit option.
- Choose the easier-to-debug option.
- Choose the layout that keeps related functionality together.
- Choose the solution with fewer hidden config dependencies.
