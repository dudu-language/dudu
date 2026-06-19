#include "dudu/module_names.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dudu {
namespace {

std::string cpp_name_piece(const std::string& text, bool pascal) {
    std::string out;
    bool upper_next = pascal;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            upper_next = pascal;
            if (!pascal && !out.empty() && out.back() != '_') {
                out.push_back('_');
            }
            continue;
        }
        if (pascal && upper_next) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            upper_next = false;
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? (pascal ? "Main" : "main") : out;
}

std::string module_cpp_prefix(const std::string& module_path, bool pascal) {
    if (module_path.empty()) {
        return pascal ? "DuduMain" : "dudu_main";
    }
    std::ostringstream out;
    if (pascal) {
        out << "Dudu";
    } else {
        out << "dudu";
    }
    size_t start = 0;
    while (start < module_path.size()) {
        const size_t dot = module_path.find('.', start);
        const size_t end = dot == std::string::npos ? module_path.size() : dot;
        const std::string piece = module_path.substr(start, end - start);
        if (pascal) {
            out << cpp_name_piece(piece, true);
        } else {
            out << '_' << cpp_name_piece(piece, false);
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return out.str();
}

} // namespace

std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                          const std::string& module_path) {
    std::filesystem::path out = base;
    size_t start = 0;
    while (start < module_path.size()) {
        const size_t dot = module_path.find('.', start);
        const size_t end = dot == std::string::npos ? module_path.size() : dot;
        out /= module_path.substr(start, end - start);
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    out += ".dd";
    return out;
}

std::string module_name_from_file(const std::filesystem::path& root,
                                  const std::filesystem::path& file) {
    std::filesystem::path relative = std::filesystem::relative(file, root);
    relative.replace_extension();
    std::ostringstream out;
    for (const std::filesystem::path& part : relative) {
        if (!out.str().empty()) {
            out << '.';
        }
        out << part.string();
    }
    return out.str();
}

std::string module_path_for_cycle(const std::filesystem::path& root,
                                  const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(file, root, error);
    if (!error && !relative.empty() && relative.native().front() != '.') {
        std::filesystem::path module = relative;
        module.replace_extension();
        std::ostringstream out;
        for (const std::filesystem::path& part : module) {
            if (!out.str().empty()) {
                out << '.';
            }
            out << part.string();
        }
        if (!out.str().empty()) {
            return out.str();
        }
    }
    return file.stem().string();
}

std::string module_cycle_message(const std::filesystem::path& root,
                                 const std::vector<std::filesystem::path>& stack,
                                 const std::filesystem::path& repeated) {
    std::ostringstream out;
    out << "cyclic module import: ";
    const auto begin = std::find(stack.begin(), stack.end(), repeated);
    bool first = true;
    for (auto it = begin == stack.end() ? stack.begin() : begin; it != stack.end(); ++it) {
        if (!first) {
            out << " -> ";
        }
        first = false;
        out << module_path_for_cycle(root, *it);
    }
    if (!first) {
        out << " -> ";
    }
    out << module_path_for_cycle(root, repeated);
    return out.str();
}

std::string generated_type_name(const std::string& module_path, const std::string& name) {
    return module_cpp_prefix(module_path, true) + cpp_name_piece(name, true);
}

std::string generated_value_name(const std::string& module_path, const std::string& name) {
    return module_cpp_prefix(module_path, false) + "_" + cpp_name_piece(name, false);
}

} // namespace dudu
