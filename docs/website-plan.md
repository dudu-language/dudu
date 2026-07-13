# Dudulang.org Website Plan

Status: implemented and deployed at <https://dudulang.org>. The source lives in
`site/`; production deployment is an explicit manual workflow through
Cloudflare Pages.

Dudu has a Cloudflare Pages site at `dudulang.org`, with source and release
artifacts hosted through GitHub.

The site is marketing and documentation, not the compiler itself. Its job is to
make the project instantly understandable, memorable, and easy to try.

The basic pitch: make a funny public language site that reads like a brown,
poo-themed satirical sibling of `mojolang.org` rather than another sterile
compiler homepage. Dudu is not satire as a language. Dudu is a real systems
language project. The public wrapper can still be satirical because weird
developer marketing is easier to remember than a bland feature matrix.

This is deliberate guerrilla marketing: the site gets attention with a dumb,
memorable visual joke, then immediately backs it with real code, real interop,
real performance goals, and an honest roadmap. Dudu should not be presented as
a joke language. The site is the joke wrapper around a sincere compiler.

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

The home page can intentionally mimic the recognizable first-page appearance of
`mojolang.org`: top nav, big language mark, hero pitch, right-side quick links,
and a few feature cards. That visual mimicry is the joke. Replace the fire/red
identity with a brown/poo-themed Dudu identity, replace the copy with Dudu copy,
and keep the implementation original.

## Dudulang.org Target

`dudulang.org` should feel like someone took the modern "Python, but systems
and accelerators" language landing-page template and made the dumbest possible
earthy version of it without making the underlying compiler fake.

The intentional gag:

- red/fire energy becomes brown/dirt/poo energy
- polished accelerator hype becomes blunt C/C++ interop pragmatism
- startup seriousness becomes weird dev humor
- the code examples and roadmap stay sincere

This is guerrilla marketing, not a language-design joke. The page should be
memorable enough that people share it, but clear enough that a visitor can tell
within seconds that Dudu is meant to be a real native language project.

The most direct reference point is `mojolang.org`: Dudu's site should read as
a humorous brown/poo-themed answer to the same public-language-site category.
The joke is visual and tonal, not technical. Mojo has fire and heat; Dudu has
ground, dirt, brown, and a dumb name. Mojo talks about Python-shaped speed;
Dudu talks about Python-shaped native C/C++ interop. The page can be
shamelessly weird because the compiler underneath is serious.

Call this guerrilla marketing, or gorilla marketing if the typo is funnier in
the moment. The important rule is that the attention hook must lead quickly to
real artifacts: source code, docs, examples, generated C++, editor tooling,
interop tests, and an honest roadmap.

## Homepage Mimic Rule

The homepage should mimic the appearance and composition of the current
`mojolang.org` front page closely enough that developers who know Mojo get the
joke immediately. Mojo is well known enough, and divisive enough, that a
brown/poo-themed Dudu version is funny before the visitor reads a paragraph.

The target is the front-page impression:

- dark page
- slim top nav
- large language logo/name in the hero
- short "write like Python, run native" style pitch
- install/quickstart button
- right-side quick links
- centered "built different" style section
- simple feature cards below

The content behind that front page must not match Mojo. Install, docs,
packages, releases, examples, roadmap, interop notes, and technical writing are
Dudu's own material. The homepage is the joke wrapper; the rest of the site is
normal Dudu documentation and project material.

The actual source, assets, screenshots, logo, copy, and implementation must be
original. The joke is "what if the fire language marketing homepage had an
earthy brown sewer cousin," not "copy the site and recolor it."

This distinction matters because Dudu is not satire as a compiler. The site is
satirical advertising around a sincere tool. If a visitor laughs and then
immediately finds real docs, examples, build instructions, and limitations, the
site is doing its job. If a visitor cannot tell whether the compiler is real,
the joke has gone too far.

## Visual Direction

- Brown, tan, amber, off-white, and near-black palette.
- Sharp UI, not bubbly toy UI.
- Brown and poo-themed, but not visually gross enough to make docs unpleasant
  to read.
- The first viewport should specifically evoke the `mojolang.org` front page:
  large Dudu mark, short pitch, install button, quick links, and the same broad
  information rhythm, but with Dudu's brown visual identity and original copy.
- Logo and mascot work can be silly, but docs and examples stay readable.
- Hero can be bold and dumb in a good way: Dudu, Python-shaped systems
  programming, C/C++ ecosystem underneath.
- Avoid making every section a poop joke. One strong joke is enough; the rest
  should explain the actual tool.
- The visual contrast to Mojo's fire/red language should be immediate: brown,
  ground, dirt, terminal grit, native toolchain pragmatism.
- Use code examples and real screenshots early. The joke gets attention; the
  compiler behavior earns the click.

The broad target for the homepage is "recognizable Mojo-front-page parody."
The broad target for every other page is "plain useful Dudu docs."

## Parody Boundaries

Acceptable:

- Similar category structure: hero, install command, docs links, examples,
  roadmap, community/GitHub links.
- Homepage-only mimicry of the `mojolang.org` front-page layout and visual
  rhythm.
- Satirical contrast against fire/speed/AI hype language.
- A brown visual identity and intentionally dumb copy in a few visible places.

Not acceptable:

- Reusing Mojo's exact copy, screenshots, logos, assets, iconography, or page
  source.
- Making install, docs, package, release, or technical content match Mojo.
- Claiming compatibility or affiliation.
- Presenting the site as an actual fork, mirror, or official variant of Mojo.
- Making the joke so heavy that the compiler looks fake.
- Hiding project status or roadmap uncertainty.

The target is homepage parody, not confusing affiliation. The user should get
the joke in one second and understand the compiler in ten seconds.

## Site Structure

Initial pages:

- Home
- Why
- Install
- Docs
- Roadmap
- GitHub

Examples and native-library interop live on the exhaustive language Tour. The old
`/why.html`, `/examples.html`, and `/interop.html` routes redirect to the relevant Tour
content so there are fewer shallow top-level pages and one consistent header.

Home page sections:

- Hero: short tagline, install command, GitHub link.
- Tour: Python-shaped syntax, C++ output, C/C++ headers and libraries, native
  performance target.
- Code examples: small Dudu snippets beside emitted C++ or command output.
- Interop: raylib, SDL3, ImGui, glm, sqlite, C stdlib, C++ stdlib.
- Roadmap: AST cleanup, native generics, modules, LSP, formatter, separate
  generated files.
- Try it: clone/build/run commands.

The dedicated Tour follows [`tour-page.md`](tour-page.md). It carries the
long-form language argument, Python and C++ comparisons, measured performance
and memory examples, deliberate omissions, portability boundaries, and the
newer type-system ideas Dudu adopts. Keep normal tooling expectations such as
the LSP out of the homepage language-feature list.

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

## Public Documentation

The Docs page is a real user manual, not a list of internal Markdown files. It
starts with a runnable quickstart, then covers the language, native interop,
projects, tests, the editor, and exact command behavior in a searchable,
anchor-addressable page. Internal implementation plans remain available as
deeper references and must not be presented as the normal learning path.

The documentation architecture, source ownership, example policy, generated
API direction, and completion criteria are defined in
[`documentation-plan.md`](documentation-plan.md). The public manual must remain
usable without JavaScript; client-side filtering and current-section tracking
are enhancements only.

## Hosting And Deployment

The canonical site is hosted by Cloudflare Pages because `dudulang.org` is
already an active Cloudflare zone. GitHub remains the source repository and
release authority.

Preferred shape:

- `site/` for source.
- `site/package.json` if a static-site framework is used.
- `.github/workflows/pages.yml` for manual direct deployment.
- Published artifact goes to the `dudu` Cloudflare Pages project.
- `dudulang.org` is the production custom domain.

Keep it static, cheap, and boring. The site should not require a backend.

Normal compiler pushes must not deploy the website. The workflow is
`workflow_dispatch` only, assembles `site/` plus the root bootstrap installer,
and uses a scoped Cloudflare token stored in GitHub Actions. This preserves
explicit release control while still making deployment reproducible.

## Implementation Checklist

- Keep the static site easy to maintain and deploy on Cloudflare Pages.
- Build a brown/poo-themed visual identity that still leaves code readable.
- Add a home page with install/run commands, a small language example, and a
  direct GitHub link.
- Add docs pages or generated links for language syntax, project driver
  commands, and roadmap.
- Keep examples and native interoperability on the long-form Why page.
- Add a status badge or short status block that says the compiler is
  experimental.
- Add manual GitHub Actions deployment to Cloudflare Pages.
- Configure `dudulang.org` as the Cloudflare Pages custom domain.
- Keep the site static and free of backend requirements.

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
