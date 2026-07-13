#!/usr/bin/env python3
"""Validate Dudu's static public site without network access."""

from __future__ import annotations

import argparse
import re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


REPO_GITHUB_PREFIX = "https://github.com/dudu-language/dudu/blob/master/"
ALLOWED_LANGUAGES = {
    "bash",
    "c",
    "cpp",
    "dudu",
    "glsl",
    "python",
    "rust",
    "toml",
}
LANGUAGE_REFERENCES = {
    "docs/collections.md",
    "docs/fixed-arrays-and-numeric-literals.md",
    "docs/compile-time-programming.md",
    "docs/generics.md",
    "docs/import_semantics.md",
    "docs/native-templates-and-macros.md",
    "docs/allocation-and-lifetimes.md",
    "docs/arrays-views-and-indexing.md",
}
CANONICAL_REFERENCES = LANGUAGE_REFERENCES | {
    "docs/known-limitations.md",
    "docs/native-compatibility-matrix.md",
}
PUBLIC_PAGES = {
    "index.html",
    "install.html",
    "tour.html",
    "docs.html",
    "roadmap.html",
}
EXPECTED_NAV = {
    "/tour.html",
    "/install.html",
    "/docs.html",
    "/roadmap.html",
}


class PageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: list[str] = []
        self.refs: list[tuple[str, str]] = []
        self.languages: list[str] = []
        self.nav_hrefs: set[str] = set()
        self._nav_depth = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {key: value or "" for key, value in attrs}
        if "id" in values:
            self.ids.append(values["id"])
        if "data-language" in values:
            self.languages.append(values["data-language"])
        if tag == "nav":
            self._nav_depth += 1
        if tag == "a" and "href" in values:
            self.refs.append(("href", values["href"]))
            if self._nav_depth:
                self.nav_hrefs.add(values["href"])
        for attribute in ("src", "href"):
            if attribute in values and not (tag == "a" and attribute == "href"):
                self.refs.append((attribute, values[attribute]))

    def handle_endtag(self, tag: str) -> None:
        if tag == "nav" and self._nav_depth:
            self._nav_depth -= 1


def parse_page(path: Path) -> PageParser:
    parser = PageParser()
    parser.feed(path.read_text(encoding="utf-8"))
    parser.close()
    return parser


def resolve_site_path(site: Path, source: Path, value: str) -> tuple[Path, str]:
    split = urlsplit(value)
    fragment = unquote(split.fragment)
    raw_path = unquote(split.path)
    if raw_path == "/":
        return site / "index.html", fragment
    if raw_path == "/install.sh":
        return site.parent / "install.sh", fragment
    if raw_path.startswith("/"):
        target = site / raw_path.removeprefix("/")
    elif raw_path:
        target = source.parent / raw_path
    else:
        target = source
    if target.is_dir():
        target /= "index.html"
    return target.resolve(), fragment


def duplicate_values(values: list[str]) -> set[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for value in values:
        if value in seen:
            duplicates.add(value)
        seen.add(value)
    return duplicates


def markdown_anchors(text: str) -> set[str]:
    anchors = set(re.findall(r'<a\s+id="([^"]+)"\s*></a>', text))
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*$", text, flags=re.MULTILINE):
        slug = heading.strip().lower()
        slug = re.sub(r"[^a-z0-9 _-]", "", slug)
        slug = re.sub(r"[ _]+", "-", slug).strip("-")
        if slug:
            anchors.add(slug)
    return anchors


def validate_markdown(repo: Path, relative: str) -> list[str]:
    source = repo / relative
    text = source.read_text(encoding="utf-8")
    errors: list[str] = []
    if relative in LANGUAGE_REFERENCES:
        for required in ('<a id="', "Previous:", "Next:", "## Limits", "## Tested Examples"):
            if required not in text:
                errors.append(f"{relative}: missing required reference marker '{required}'")

    prose = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
    prose = re.sub(r"`[^`\n]*`", "", prose)
    links = re.findall(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+[^)]*)?\)", prose)
    for value in links:
        split = urlsplit(value)
        if split.scheme or split.netloc:
            continue
        target = source if not split.path else source.parent / unquote(split.path)
        target = target.resolve()
        if not target.exists():
            errors.append(f"{relative}: broken Markdown link '{value}'")
            continue
        if split.fragment and target.suffix == ".md":
            target_text = target.read_text(encoding="utf-8")
            if unquote(split.fragment) not in markdown_anchors(target_text):
                errors.append(f"{relative}: missing Markdown anchor '{value}'")
    return errors


def validate(repo: Path) -> list[str]:
    site = repo / "site"
    pages = {path.name: parse_page(path) for path in sorted(site.glob("*.html"))}
    errors: list[str] = []
    referenced_repo_docs: set[str] = set()

    for name, parser in pages.items():
        source = site / name
        for duplicate in duplicate_values(parser.ids):
            errors.append(f"{source.relative_to(repo)}: duplicate id '{duplicate}'")
        for language in parser.languages:
            if language not in ALLOWED_LANGUAGES:
                errors.append(
                    f"{source.relative_to(repo)}: unsupported data-language '{language}'"
                )
        if name in PUBLIC_PAGES and not EXPECTED_NAV.issubset(parser.nav_hrefs):
            missing = ", ".join(sorted(EXPECTED_NAV - parser.nav_hrefs))
            errors.append(f"{source.relative_to(repo)}: missing navigation links: {missing}")

        for attribute, value in parser.refs:
            if not value or value.startswith(("mailto:", "tel:", "javascript:")):
                continue
            if value.startswith(REPO_GITHUB_PREFIX):
                relative = unquote(value.removeprefix(REPO_GITHUB_PREFIX)).split("#", 1)[0]
                referenced_repo_docs.add(relative)
                if not (repo / relative).exists():
                    errors.append(
                        f"{source.relative_to(repo)}: missing repository target '{relative}'"
                    )
                if relative.startswith("docs/") and "plan" in Path(relative).stem:
                    errors.append(
                        f"{source.relative_to(repo)}: implementation plan exposed as public documentation: {relative}"
                    )
                continue
            split = urlsplit(value)
            if split.scheme or split.netloc:
                continue
            target, fragment = resolve_site_path(site, source, value)
            if not target.exists():
                errors.append(
                    f"{source.relative_to(repo)}: broken {attribute} target '{value}'"
                )
                continue
            if fragment and target.suffix == ".html":
                target_parser = pages.get(target.name)
                if target_parser is None or fragment not in target_parser.ids:
                    errors.append(
                        f"{source.relative_to(repo)}: missing fragment '#{fragment}' in {target.name}"
                    )

    missing_references = CANONICAL_REFERENCES - referenced_repo_docs
    for reference in sorted(missing_references):
        errors.append(f"site/docs.html: canonical reference is not linked: {reference}")

    for reference in sorted(LANGUAGE_REFERENCES):
        errors.extend(validate_markdown(repo, reference))

    docs_text = (site / "docs.html").read_text(encoding="utf-8")
    tour_text = (site / "tour.html").read_text(encoding="utf-8")
    if "delete(" in docs_text:
        errors.append("site/docs.html: stale function-shaped delete syntax")
    indexing_url = REPO_GITHUB_PREFIX + "docs/arrays-views-and-indexing.md"
    if indexing_url not in docs_text or indexing_url not in tour_text:
        errors.append("indexing tutorial must be linked from both docs.html and tour.html")

    syntax = (site / "assets/syntax.js").read_text(encoding="utf-8")
    for language in ("dudu", "toml"):
        if f"Prism.languages.{language}" not in syntax:
            errors.append(f"site/assets/syntax.js: missing Prism language '{language}'")
    for language in ALLOWED_LANGUAGES - {"dudu", "toml"}:
        if not (site / f"vendor/prism/prism-{language}.min.js").exists():
            errors.append(f"site/vendor/prism: missing '{language}' grammar")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "repo",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
    )
    args = parser.parse_args()
    repo = args.repo.resolve()
    errors = validate(repo)
    if errors:
        for error in errors:
            print(f"site check: {error}")
        return 1
    page_count = len(list((repo / "site").glob("*.html")))
    print(f"site structure passed: {page_count} HTML pages, links, anchors, and grammars")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
