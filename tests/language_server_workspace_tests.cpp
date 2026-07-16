#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_workspace.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

dudu::Document document(const std::filesystem::path& path, std::string text) {
    return {.uri = dudu::file_uri(path), .path = path, .text = std::move(text)};
}

void test_explicit_workspace_does_not_scan_scratch_parent() {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / "dudu_lsp_workspace_roots_test";
    std::filesystem::remove_all(base);

    const std::filesystem::path workspace = base / "workspace";
    const std::filesystem::path visible = workspace / "visible.dd";
    const std::filesystem::path unrelated = base / "lsp_import_target.dd";
    const std::filesystem::path scratch = base / "scratch.dd";
    write_file(visible, "def visible() -> i32:\n    return 1\n");
    write_file(unrelated, "def missing_helper() -> i32:\n    return 2\n");
    write_file(scratch, "def scratch() -> i32:\n    return 3\n");

    const dudu::Document open = document(scratch, "def scratch() -> i32:\n    return 3\n");
    const std::map<std::string, dudu::Document> found =
        dudu::workspace_documents({{open.uri, open}}, {workspace});

    assert(found.contains(open.uri));
    assert(found.contains(dudu::file_uri(visible)));
    assert(!found.contains(dudu::file_uri(unrelated)));
    std::filesystem::remove_all(base);
}

void test_project_manifest_defines_workspace_without_client_root() {
    const std::filesystem::path project =
        std::filesystem::temp_directory_path() / "dudu_lsp_manifest_workspace_test";
    std::filesystem::remove_all(project);

    const std::filesystem::path main = project / "main.dd";
    const std::filesystem::path sibling = project / "sibling.dd";
    write_file(project / "dudu.toml", "name = \"workspace_test\"\nentry = \"main.dd\"\n");
    write_file(main, "def main() -> i32:\n    return 0\n");
    write_file(sibling, "def sibling() -> i32:\n    return 1\n");

    const dudu::Document open = document(main, "def main() -> i32:\n    return 0\n");
    const std::map<std::string, dudu::Document> found =
        dudu::workspace_documents({{open.uri, open}}, {});

    assert(found.contains(dudu::file_uri(sibling)));
    std::filesystem::remove_all(project);
}

} // namespace

int main() {
    try {
        test_explicit_workspace_does_not_scan_scratch_parent();
        test_project_manifest_defines_workspace_without_client_root();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
