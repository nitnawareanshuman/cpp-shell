#include "shell/path.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unistd.h>

namespace shell {

bool find_executable(const std::string &command, std::string &executable_path) {
  if (command.find('/') != std::string::npos) {
    if (access(command.c_str(), X_OK) == 0) {
      executable_path = command;
      return true;
    }
    return false;
  }

  const char *pathvar = std::getenv("PATH");
  if (pathvar == nullptr) {
    return false;
  }

  std::istringstream path_stream(pathvar);
  std::string directory;
  while (std::getline(path_stream, directory, ':')) {
    if (directory.empty()) {
      directory = ".";
    }
    std::string candidate =
        (std::filesystem::path(directory) / command).string();
    if (access(candidate.c_str(), X_OK) == 0) {
      executable_path = candidate;
      return true;
    }
  }
  return false;
}

std::vector<std::string> list_path_executables() {
  std::vector<std::string> names;
  const char *pathvar = std::getenv("PATH");
  if (pathvar == nullptr) {
    return names;
  }

  std::istringstream path_stream(pathvar);
  std::string directory;
  while (std::getline(path_stream, directory, ':')) {
    std::error_code ec;
    if (directory.empty()) {
      directory = ".";
    }
    for (const auto &entry :
         std::filesystem::directory_iterator(directory, ec)) {
      if (ec) {
        break;
      }
      std::error_code file_ec;
      if (!entry.is_regular_file(file_ec) && !entry.is_symlink(file_ec)) {
        continue;
      }
      const std::string name = entry.path().filename().string();
      const std::string full = entry.path().string();
      if (access(full.c_str(), X_OK) == 0) {
        names.push_back(name);
      }
    }
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

std::string quote_for_shell(const std::string &value) {
  if (value.empty()) {
    return "''";
  }
  bool special = false;
  for (char ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) || ch == '\'' ||
        ch == '"' || ch == '\\' || ch == '$' || ch == '|' || ch == '&' ||
        ch == '<' || ch == '>' || ch == ';' || ch == '*' || ch == '?' ||
        ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '{' ||
        ch == '}' || ch == '~' || ch == '`') {
      special = true;
      break;
    }
  }
  if (!special) {
    return value;
  }
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out += ch;
    }
  }
  out += "'";
  return out;
}

} // namespace shell
