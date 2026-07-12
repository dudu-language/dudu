# Releasing Dudu

Releases are deliberate tag operations. Ordinary pushes to `master` do not
publish artifacts or run deployment automation.

## Version Sources

`VERSION` is the canonical toolchain version. It feeds `dudu`, `duc`,
`dudu-lsp`, generated C++ metadata, and editor metadata through
`scripts/sync_version.py`.

Before cutting a release:

1. Set the intended semantic version in `VERSION`.
2. Run `./scripts/sync_version.py --write`.
3. Move relevant `CHANGELOG.md` entries from `[Unreleased]` under the exact
   release version and date.
4. Commit those changes.

The VS Code Marketplace uses a numeric extension version and its prerelease
channel, so an alpha toolchain such as `0.1.0-alpha.1` maps to extension
metadata `0.1.0`.

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

The later alpha distribution gate produces the immutable source archive,
SHA-256 checksum, source-building bootstrap installer, and prerelease VSIX from
that tag. Those artifacts must never be built from a moving branch reference.

See [the distribution plan](distribution-plan.md) for ownership, update,
rollback, package-channel, and host-matrix rules.
