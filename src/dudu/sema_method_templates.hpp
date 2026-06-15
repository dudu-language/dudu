#pragma once

#include <string>
#include <vector>

namespace dudu {

std::vector<std::string> template_args_from_type(const std::string& type);
std::string substitute_method_template_type(std::string type,
                                            const std::vector<std::string>& generic_params,
                                            const std::vector<std::string>& args);
std::string substitute_receiver_template_type(std::string type,
                                              const std::vector<std::string>& receiver_args);
std::string substitute_class_template_type(std::string type,
                                           const std::vector<std::string>& generic_params,
                                           const std::vector<std::string>& receiver_args);

} // namespace dudu
