#pragma once

#include <string>
#include <vector>

namespace shell {

bool find_executable(const std::string &command, std::string &executable_path);
std::vector<std::string> list_path_executables();
std::string quote_for_shell(const std::string &value);

} // namespace shell
