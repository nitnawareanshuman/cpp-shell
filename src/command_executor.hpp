#ifndef COMMAND_EXECUTOR_HPP
#define COMMAND_EXECUTOR_HPP

#include <string>

struct CommandResult
{
    std::string output;
    std::string error;
    bool should_exit = false;
};

CommandResult executeCommand(const std::string &input);

#endif