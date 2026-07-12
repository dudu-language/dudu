# Dudu for VS Code

Language support for [Dudu](https://github.com/wegfawefgawefg/dudu), the
Python-shaped systems language with direct C and C++ interop.

The extension provides:

- semantic highlighting and diagnostics
- formatting
- hover, definition, references, completion, and signature help
- inlay hints
- project build, run, check, and test commands

## Requirement

Install a matching Dudu toolchain and ensure `dudu` and `dudu-lsp` are on
`PATH`. Custom executable paths can be set with `dudu.path` and
`dudu.lspPath`.

The extension does not bundle the compiler or language server.

## Commands

Open the command palette and search for `Dudu` to format, check, build, run,
test, restart the language server, or toggle inlay hints.

## License

Dudu is available under either the MIT License or Apache License 2.0, at your
option.
