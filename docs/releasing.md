# Releasing Dudu

Releases are deliberate tag operations. Ordinary pushes to `master` do not
publish artifacts or run deployment automation.

## Version Sources

`VERSION` is the canonical toolchain version. It feeds `dudu`, `duc`,
`dudu-lsp`, generated C++ metadata, and editor metadata through
`scripts/sync_version.py`.

The extension registry version is the numeric `version` in
`editors/vscode/package.json`. Marketplace and Open VSX do not accept Dudu's
SemVer prerelease suffix, and they reject publishing the same numeric version
twice. Every Dudu release therefore sets a new numeric extension version and
then runs `scripts/sync_version.py --write`; the package also records the exact
minimum Dudu toolchain version.

Before cutting a release:

1. Set the intended semantic version in `VERSION`.
2. Set a new numeric version in `editors/vscode/package.json`.
3. Run `./scripts/sync_version.py --write`.
4. Move relevant `CHANGELOG.md` entries from `[Unreleased]` under the exact
   release version and date.
5. Commit those changes.

The first alpha uses extension version `0.1.0`. A later alpha must use another
numeric version such as `0.1.1`; changing only the Dudu prerelease suffix is
not sufficient for either registry.

## Local Gate

From a clean commit, run:

```sh
./scripts/release-check.sh
```

The command runs source/version guards, the full compiler and LSP matrix,
optional native probes available on the host, dogfood projects, a clean
`Release` build/install, installed hello and C/C++ import programs, and
dependency diagnostics. `--allow-dirty` exists only while developing the gate;
it is not valid release evidence.

Do not tag around a failure or convert an available probe into a skip merely to
cut a release.

## Tag Cut

After the local gate passes and `git status --short` is empty:

```sh
version="$(cat VERSION)"
git tag -s "v$version" -m "Dudu $version"
git push origin "v$version"
```

If signing is not configured, establish the project signing policy before the
first public tag rather than silently creating an unsigned official tag.

## Artifact And Publication Workflows

`.github/workflows/release.yml` is the only hosted release producer. It runs
only for a pushed tag or an explicit manual invocation naming an existing tag.
It never runs on ordinary `master` pushes.

The workflow:

1. verifies that `vVERSION`, the checked-out commit, and the immutable tag
   agree
2. repeats the release gate on Linux
3. builds the source archive, checksum, manifest, VSIX, `.deb`, AUR recipe, and
   Homebrew formula
4. runs the package and two-version installer lifecycle checks
5. repeats source and lifecycle validation on an Apple Silicon `macos-14`
   runner
6. publishes a GitHub prerelease only after both hosts pass

The manual workflow has a `publish` switch. Leave it off when validating an
existing tag without modifying GitHub Releases.

Editor registries are deliberately separate. After the GitHub prerelease
exists, `.github/workflows/publish-editors.yml` downloads its VSIX, verifies it
against the release manifest, and publishes the same bytes to Marketplace and
Open VSX. It requires `VSCE_PAT` and `OVSX_PAT` repository secrets. Missing
credentials cannot block the compiler/toolchain release.

The generated package channels are:

- `dist/recipes/homebrew/dudu.rb` for `wegfawefgawefg/homebrew-dudu`
- `dist/recipes/aur/PKGBUILD` and `.SRCINFO` for AUR package `dudu`
- `dist/dudu_VERSION_ARCH.deb` for direct download

All consume or contain the same tagged Dudu version. Their CMake builds stamp
`homebrew`, `aur`, or `deb` ownership so `dudu update` cannot overwrite files
owned by the package manager.

Those artifacts must never be built from a moving branch reference.

See [the distribution plan](distribution-plan.md) for ownership, update,
rollback, package-channel, and host-matrix rules.
