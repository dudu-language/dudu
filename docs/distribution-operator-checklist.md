# Distribution Operator Checklist

This checklist contains the account, identity, payment, and secret-management
work that cannot be completed safely by an automated contributor. The
technical release implementation remains specified in
[Distribution Plan](distribution-plan.md).

Do not paste access tokens, private keys, recovery codes, or payment details
into chat, issues, commits, or shell history. Prefer interactive CLI prompts
and GitHub Actions secrets.

## Current Status

| Service | Current state | Operator action |
| --- | --- | --- |
| GitHub | Organization `dudu-language` owns `dudu`; `wegfawefgawefg` is an active Owner | None for repository releases |
| Release signing | No dedicated signing key exists | Optional for the first alpha; create one before signed releases |
| Visual Studio Marketplace | Publisher `dudu` exists under the display name `Dudu Language` | Upload the release VSIX manually |
| Open VSX | Publisher Agreement signed; namespace `dudu` claimed; `OVSX_PAT` stored in GitHub Actions | None |
| AUR | Dedicated local SSH key exists; upstream registration is temporarily disabled | Register and add the public key when Arch reopens registration |
| Homebrew | Public tap repository `dudu-language/homebrew-dudu` exists | None |
| `.deb` download | GitHub access is sufficient | No account setup |
| Website domain | `dudulang.org` is owned; `dudulang.com` is occupied by an unrelated site | None |
| Cloudflare Pages | `dudu` project deployed; `dudulang.org` and `www.dudulang.org` are active with HTTPS | None |

Availability and account state can change. Recheck them immediately before
purchase or publication.

## 1. Visual Studio Marketplace

The publisher was created using:

```text
Name: Dudu Language
ID: dudu
Description: The Python-shaped systems language with direct C and C++ interop.
```

The publisher identity matches the extension manifest. Domain verification can
be completed after `dudulang.org` serves the project website and any required
ownership record has been added.

Publish the first alpha manually rather than creating an Azure subscription for
a global Personal Access Token that Microsoft retires on December 1, 2026:

1. Download `dudu-VERSION.vsix` and `dudu-VERSION-manifest.txt` from the tagged
   GitHub prerelease.
2. Verify the VSIX SHA-256 value against `vsix_sha256` in the manifest.
3. Open the `dudu` Marketplace publisher page and choose **New extension**.
4. Upload that exact verified VSIX and publish it as a pre-release.

The publisher account must retain a recoverable human owner. Revisit Microsoft
Entra-based automation only when repeated releases justify the Azure
infrastructure.

## 2. Open VSX

The Open VSX account is linked to the Eclipse Foundation account, the Publisher
Agreement is signed, namespace `dudu` is claimed, and its publishing token is
stored as the `OVSX_PAT` repository secret.

Confirm the secret exists without attempting to retrieve its value:

```sh
cd /home/vega/Coding/LangDev/Dudu/dudu
gh secret list --repo dudu-language/dudu
```

Rotate the token from Open VSX and replace the GitHub secret if it is ever
exposed. Do not place it in `.env`, `package.json`, shell startup files, or the
repository.

## 3. AUR

New AUR account registration is temporarily disabled upstream. A dedicated
SSH key already exists at `~/.ssh/aur`; do not replace it or use the desktop
GitHub key. When registration reopens, display its public half with:

```sh
cat ~/.ssh/aur.pub
```

Add only the displayed public key to the AUR account. Keep `~/.ssh/aur`
private and backed up. Add this local SSH configuration:

```sshconfig
Host aur.archlinux.org
    IdentityFile ~/.ssh/aur
    User aur
```

No AUR password or private key is needed in the Dudu repository. Initial AUR
publication is normally performed from this machine after the GitHub tag and
source checksum exist.

## 4. Release Signing

The first alpha requires immutable tags and SHA-256 checksums, but a signing
key is not an alpha blocker. Do not reuse `~/.ssh/id_ed25519` as the long-term
release identity.

When signed releases are enabled, create a dedicated key with a strong
passphrase and make two encrypted backups. Losing every private-key copy does
not remove access to GitHub or publishing accounts, but it prevents producing
new signatures under that identity. A replacement key can be announced and
used for later releases; old signatures remain valid.

Git supports either GPG or SSH signatures. A dedicated SSH signing key is the
simpler initial choice for this project:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/dudu-release-signing \
    -C "Dudu release signing"
gh ssh-key add ~/.ssh/dudu-release-signing.pub \
    --type signing --title "Dudu release signing"
```

Do not add the private key to GitHub Actions until automated signing is
explicitly designed. Local signed tags avoid putting the release authority in
a hosted CI secret.

## 5. Domain And DNS

`dudulang.com` is occupied by an unrelated website. Dudu owns
`dudulang.org`, which is the canonical public identity.

The website deployment is complete:

1. Ensure registrar account MFA and recovery information remain current.
2. Keep the GitHub Actions Cloudflare token scoped to Pages and the
   `dudulang.org` zone.
3. Use the manual `Deploy Dudu website` workflow for intentional production
   updates.

The `dudu` Cloudflare Pages project serves both `dudulang.org` and
`www.dudulang.org`. The apex domain is canonical, HTTP redirects to HTTPS, and
the deployed `/install.sh` is assembled directly from the repository root.

The local scoped Cloudflare credential can read the active `dudulang.org`
zone. Domain purchase, billing, account recovery, and credential rotation
remain human responsibilities.

## 6. GitHub Release Channels

No additional account is required for GitHub Releases, downloadable `.deb`
files, source archives, VSIX attachments, or a personal Homebrew tap. The
current authenticated GitHub account can create these.

Before publication, the operator must confirm that these public identities are
acceptable:

```text
Repository: dudu-language/dudu
Homebrew tap: dudu-language/homebrew-dudu
Extension publisher: dudu
Open VSX namespace: dudu
AUR package: dudu
Domain: dudulang.org
```

Changing one after publication creates migration work, so resolve any naming
objection before cutting `0.1.0-alpha.4`.

## Operator Handoff

When the account work is complete, report only these non-secret facts:

- Marketplace publisher created: yes/no and exact ID
- Open VSX namespace created: yes/no and exact namespace
- `OVSX_PAT` visible in `gh secret list`: yes/no
- AUR account and public key configured: yes/no
- release signing enabled: yes/no
- `dudulang.org` DNS ready for the website: yes/no
- public identity names above approved: yes/no

That is sufficient to finish wiring and publishing without exposing any
credential material.
