
#ifndef SHELL_HPP
#define SHELL_HPP

#include <string>
#include <vector>

struct CommandResult
{
    std::string output;
    std::string error;
    bool should_exit = false;
};

class Shell
{
public:
    Shell() = default;

    CommandResult execute(
        const std::string &input,
        bool capture_external_output = false);

private:
    bool parseArguments(
        const std::string &input,
        std::vector<std::string> &args,
        std::string &error);

    CommandResult executeExternal(
        const std::vector<std::string> &args,
        bool capture_output);

    std::string findExecutable(const std::string &command);
};

#endif
