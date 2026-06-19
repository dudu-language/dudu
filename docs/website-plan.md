# Website Plan

Dudu should have a GitHub Pages site intended for `dudulang.org`.

The site is marketing and documentation, not the compiler itself. Its job is to
make the project instantly understandable, memorable, and easy to try.

## Tone

The public site should be a humorous, satirical cousin of modern language
landing pages. The rough joke is:

- Mojo advertises fire, speed, Python-like syntax, and serious accelerator
  ambitions.
- Dudu advertises brown, dirt, questionable taste, Python-like syntax, and
  serious native C/C++ interop ambitions.

Dudu itself is not a joke language. The marketing can be weird because modern
developer tools need a hook. Treat the visual theme as guerrilla marketing:
funny enough to remember, serious enough that a developer can still evaluate
the language.

The site should not copy Mojo text, logos, illustrations, or layout details
verbatim. It can parody the category and contrast against the fire/red visual
language with its own brown/poo-themed identity.

## Visual Direction

- Brown, tan, amber, off-white, and near-black palette.
- Sharp UI, not bubbly toy UI.
- Logo and mascot work can be silly, but docs and examples stay readable.
- Hero can be bold and dumb in a good way: Dudu, Python-shaped systems
  programming, C/C++ ecosystem underneath.
- Avoid making every section a poop joke. One strong joke is enough; the rest
  should explain the actual tool.

## Site Structure

Initial pages:

- Home
- Install
- Docs
- Examples
- Interop
- Roadmap
- GitHub

Home page sections:

- Hero: short tagline, install command, GitHub link.
- Why: Python-shaped syntax, C++ output, C/C++ headers and libraries, native
  performance target.
- Code examples: small Dudu snippets beside emitted C++ or command output.
- Interop: raylib, SDL3, ImGui, glm, sqlite, C stdlib, C++ stdlib.
- Roadmap: AST cleanup, native generics, modules, LSP, formatter, separate
  generated files.
- Try it: clone/build/run commands.

## GitHub Pages

The repo should publish the site from a normal docs/site source tree and a
GitHub Actions workflow.

Preferred shape:

- `site/` for source.
- `site/package.json` if a static-site framework is used.
- `.github/workflows/pages.yml` for deploy.
- Published artifact goes to GitHub Pages.

Keep it static, cheap, and boring. The site should not require a backend.

## Content Requirements

The site must be honest about current status:

- Dudu is experimental.
- It compiles to C++.
- It is intended to interop deeply with C and C++ libraries.
- Some compiler architecture work is still in progress.
- The real roadmap lives in repo docs.

The site should link to:

- `docs/le_plan.md`
- `docs/destringing-goals.md`
- `docs/header-awareness-plan.md`
- `docs/project-driver-plan.md`
- `docs/language.md`

## Non-Goals

- Do not pretend Dudu is production-ready before it is.
- Do not copy another language site's assets or exact prose.
- Do not make a heavy web app.
- Do not make the docs depend on JavaScript.
