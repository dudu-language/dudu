# Website Plan

Dudu should have a GitHub Pages site intended for `dudulang.org`.

The site is marketing and documentation, not the compiler itself. Its job is to
make the project instantly understandable, memorable, and easy to try.

The basic pitch: make a funny public language site that reads like a brown,
poo-themed satirical sibling of `mojolang.org` rather than another sterile
compiler homepage. Dudu is not satire as a language. Dudu is a real systems
language project. The public wrapper can still be satirical because weird
developer marketing is easier to remember than a bland feature matrix.

This is deliberate guerrilla marketing: the site gets attention with a dumb,
memorable visual joke, then immediately backs it with real code, real interop,
real performance goals, and an honest roadmap.

## Tone

The public site should be a humorous, satirical cousin of modern language
landing pages, especially the current high-energy Python-but-fast language
category. The rough joke is:

- Mojo advertises fire, speed, Python-like syntax, and serious accelerator
  ambitions.
- Dudu advertises brown, dirt, questionable taste, Python-like syntax, and
  serious native C/C++ interop ambitions.

Dudu itself is not a joke language. The marketing can be weird because modern
developer tools need a hook. Treat the visual theme as guerrilla marketing in
the stupidest possible spelling: memorable, a little wrong, and still backed by
real engineering. It should be funny enough to remember and serious enough that
a developer can still evaluate the language.

The tone should avoid polished startup voice. No grandiose AI-adjacent claims,
no ad-copy rhythm, no fake inevitability. It should sound like someone built a
serious compiler and then made the least tasteful public wrapper that still
communicates the idea clearly.

The site should not copy Mojo text, logos, illustrations, or layout details
verbatim. It can parody the category and contrast against the fire/red visual
language with its own brown/poo-themed identity. Think "recognizable genre
spoof," not trademark-confusing clone.

## Visual Direction

- Brown, tan, amber, off-white, and near-black palette.
- Sharp UI, not bubbly toy UI.
- The first viewport should feel recognizably related to the modern
  Python-but-fast compiler landing-page genre, while being visually inverted:
  brown instead of red, ground instead of fire, native pragmatism instead of
  heat metaphors.
- Logo and mascot work can be silly, but docs and examples stay readable.
- Hero can be bold and dumb in a good way: Dudu, Python-shaped systems
  programming, C/C++ ecosystem underneath.
- Avoid making every section a poop joke. One strong joke is enough; the rest
  should explain the actual tool.
- The visual contrast to Mojo's fire/red language should be immediate: brown,
  ground, dirt, terminal grit, native toolchain pragmatism.
- Use code examples and real screenshots early. The joke gets attention; the
  compiler behavior earns the click.

## Parody Boundaries

Acceptable:

- Similar category structure: hero, install command, docs links, examples,
  roadmap, community/GitHub links.
- Satirical contrast against fire/speed/AI hype language.
- A brown visual identity and intentionally dumb copy in a few visible places.

Not acceptable:

- Reusing Mojo's exact copy, screenshots, logos, assets, iconography, or page
  composition.
- Claiming compatibility or affiliation.
- Presenting the site as an actual fork, mirror, or official variant of Mojo.
- Making the joke so heavy that the compiler looks fake.
- Hiding project status or roadmap uncertainty.

The target is parody of the genre, not a confusing clone. The user should get
the joke in one second and understand the compiler in ten seconds.

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

Suggested hero copy direction:

```text
Dudu
Python-shaped systems programming.
C and C++ underneath. Brown on purpose.
```

Suggested small-print tone:

```text
Experimental. Native. Weirdly sincere.
```

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
