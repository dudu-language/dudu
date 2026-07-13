#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_wire.hpp"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>

namespace dudu::macro {

using ExpansionDispatch =
    std::function<protocol::ExpansionResponse(const protocol::ExpansionRequest&)>;

std::optional<wire::Frame> read_worker_frame(std::istream& input,
                                             const wire::DecodeLimits& limits = {});
void write_worker_frame(std::ostream& output, const wire::Frame& frame);

int serve_worker(std::istream& input, std::ostream& output, const protocol::MacroCatalog& catalog,
                 const ExpansionDispatch& dispatch, const wire::DecodeLimits& limits = {});
int serve_worker(std::istream& input, std::ostream& output, const protocol::MacroCatalog& catalog,
                 const std::filesystem::path& project_root, const ExpansionDispatch& dispatch,
                 const wire::DecodeLimits& limits = {});
int serve_worker(const protocol::MacroCatalog& catalog, const ExpansionDispatch& dispatch,
                 const wire::DecodeLimits& limits = {});
int serve_worker(const protocol::MacroCatalog& catalog, const std::filesystem::path& project_root,
                 const ExpansionDispatch& dispatch, const wire::DecodeLimits& limits = {});

} // namespace dudu::macro
