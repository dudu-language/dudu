#pragma once

#include <string>
#include <vector>

namespace dudu {

std::string dudu_type(std::string type);
std::vector<std::string> signature_params(const std::string& signature);
std::string signature_return_type(const std::string& signature);
std::string signature_receiver_type(const std::string& signature);

} // namespace dudu
