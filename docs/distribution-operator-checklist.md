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
| GitHub | `gh` is authenticated as `wegfawefgawefg` | None for repository releases |
| Release signing | No dedicated signing key exists | Optional for the first alpha; create one before signed releases |
| Visual Studio Marketplace | Publisher creation page reached | Create publisher `dudu` |
| Open VSX | No namespace or token configured | Create namespace and token |
| AUR | No account or dedicated SSH key configured | Create account and add a dedicated public key |
| Homebrew | GitHub access is sufficient | No account setup; authorize creation of a tap repository when ready |
| `.deb` download | GitHub access is sufficient | No account setup |
| Website domain | `dudulang.org` is owned; `dudulang.com` is occupied by an unrelated site | None |
| Cloudflare | `dudulang.org` is active and unpaused on Cloudflare | Configure GitHub Pages DNS when the site is ready |

Availability and account state can change. Recheck them immediately before
purchase or publication.

## 1. Visual Studio Marketplace

Create the publisher using:

```text
Name: Dudu Language
ID: dudu
Description: The Python-shaped systems language with direct C and C++ interop.
```

Leave the verified domain empty until Dudu owns a domain. If `dudu` is not
available, stop before choosing another ID because the extension manifest and
release automation must use the exact publisher ID.

After creation, provide automation access without sharing the credential in
chat:

1. Create the publishing credential required by the current Marketplace
   process.
2. Store it through an interactive prompt:

   ```sh
   cd /home/vega/Coding/LangDev/Dudu/dudu
   gh secret set VSCE_PAT
   ```

3. Confirm only that the secret exists:

   ```sh
   gh secret list
   ```

Marketplace credentials should have only the extension-publishing permissions
required for the `dudu` publisher and should have an expiration date. The
publisher account must retain a recoverable human owner.

## 2. Open VSX

Create or sign into an Open VSX account, claim the `dudu` namespace, and create
a publishing token. If the namespace is unavailable, stop before selecting an
alternative so package identity remains consistent across registries.

Store the token interactively:

```sh
cd /home/vega/Coding/LangDev/Dudu/dudu
gh secret set OVSX_PAT
gh secret list
```

Do not place the token in `.env`, `package.json`, shell startup files, or the
repository.

## 3. AUR

Create an AUR account and use a dedicated SSH key rather than the desktop
GitHub key:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/aur -C "dudu AUR publishing"
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

The remaining operator work is:

1. Ensure registrar account MFA and recovery information remain current.
2. Confirm when the website is ready; automation can then configure GitHub
   Pages records and the repository `CNAME`.

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
Repository: wegfawefgawefg/dudu
Homebrew tap: wegfawefgawefg/homebrew-dudu
Extension publisher: dudu
Open VSX namespace: dudu
AUR package: dudu
Domain: dudulang.org
```

Changing one after publication creates migration work, so resolve any naming
objection before cutting `0.1.0-alpha.1`.

## Operator Handoff

When the account work is complete, report only these non-secret facts:

- Marketplace publisher created: yes/no and exact ID
- Open VSX namespace created: yes/no and exact namespace
- `VSCE_PAT` and `OVSX_PAT` visible in `gh secret list`: yes/no
- AUR account and public key configured: yes/no
- release signing enabled: yes/no
- `dudulang.org` DNS ready for the website: yes/no
- public identity names above approved: yes/no

That is sufficient to finish wiring and publishing without exposing any
credential material.
