
#include "shell.hpp"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool Shell::parseArguments(
    const std::string &input,
    std::vector<std::string> &args,
    std::string &error)
{
    std::string word;
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;
    bool wordStarted = false;

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const char ch = input[i];

        if (ch == '\\' && inDoubleQuotes)
        {
            if (i + 1 < input.size())
            {
                const char next = input[i + 1];

                if (next == '"' || next == '\\')
                {
                    word += next;
                    ++i;
                }
                else
                {
                    word += ch;
                }
            }
            else
            {
                word += ch;
            }

            wordStarted = true;
        }
        else if (ch == '\\' && !inSingleQuotes && !inDoubleQuotes)
        {
            wordStarted = true;

            if (i + 1 < input.size())
            {
                word += input[++i];
            }
        }
        else if (ch == '\'' && !inDoubleQuotes)
        {
            inSingleQuotes = !inSingleQuotes;
            wordStarted = true;
        }
        else if (ch == '"' && !inSingleQuotes)
        {
            inDoubleQuotes = !inDoubleQuotes;
            wordStarted = true;
        }
        else if (
            std::isspace(static_cast<unsigned char>(ch)) &&
            !inSingleQuotes &&
            !inDoubleQuotes)
        {
            if (wordStarted)
            {
                args.push_back(word);
                word.clear();
                wordStarted = false;
            }
        }
        else
        {
            word += ch;
            wordStarted = true;
        }
    }

    if (inSingleQuotes)
    {
        error = "Unclosed single quote\n";
        return false;
    }

    if (inDoubleQuotes)
    {
        error = "Unclosed double quote\n";
        return false;
    }

    if (wordStarted)
    {
        args.push_back(word);
    }

    return true;
}

std::string Shell::findExecutable(const std::string &command)
{
    if (command.empty())
    {
        return {};
    }

    if (command.find('/') != std::string::npos)
    {
        if (access(command.c_str(), X_OK) == 0)
        {
            return command;
        }

        return {};
    }

    const char *pathValue = std::getenv("PATH");

    if (pathValue == nullptr)
    {
        return {};
    }

    std::istringstream pathStream(pathValue);
    std::string directory;

    while (std::getline(pathStream, directory, ':'))
    {
        if (directory.empty())
        {
            directory = ".";
        }

        const std::string filepath = directory + "/" + command;

        if (access(filepath.c_str(), X_OK) == 0)
        {
            return filepath;
        }
    }

    return {};
}

CommandResult Shell::executeExternal(
    const std::vector<std::string> &args,
    bool capture_output)
{
    CommandResult result;

    if (args.empty())
    {
        return result;
    }

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (const std::string &arg : args)
    {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }

    argv.push_back(nullptr);

    int pipefd[2] = {-1, -1};

    if (capture_output && pipe(pipefd) == -1)
    {
        result.error = "pipe: ";
        result.error += std::strerror(errno);
        result.error += '\n';
        return result;
    }

    const pid_t pid = fork();

    if (pid == -1)
    {
        const int savedErrno = errno;

        if (capture_output)
        {
            close(pipefd[0]);
            close(pipefd[1]);
        }

        result.error = "fork: ";
        result.error += std::strerror(savedErrno);
        result.error += '\n';

        return result;
    }

    if (pid == 0)
    {
        if (capture_output)
        {
            close(pipefd[0]);

            if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
                dup2(pipefd[1], STDERR_FILENO) == -1)
            {
                _exit(126);
            }

            close(pipefd[1]);
        }

        execvp(argv[0], argv.data());

        const int savedErrno = errno;

        if (savedErrno == ENOENT)
        {
            dprintf(
                STDERR_FILENO,
                "%s: command not found\n",
                args[0].c_str());
        }
        else
        {
            dprintf(
                STDERR_FILENO,
                "%s: %s\n",
                args[0].c_str(),
                std::strerror(savedErrno));
        }

        _exit(savedErrno == ENOENT ? 127 : 126);
    }

    if (capture_output)
    {
        close(pipefd[1]);

        char buffer[4096];

        while (true)
        {
            const ssize_t bytesRead =
                read(pipefd[0], buffer, sizeof(buffer));

            if (bytesRead > 0)
            {
                result.output.append(buffer, bytesRead);
            }
            else if (bytesRead == 0)
            {
                break;
            }
            else if (errno != EINTR)
            {
                result.error = "read: ";
                result.error += std::strerror(errno);
                result.error += '\n';
                break;
            }
        }

        close(pipefd[0]);
    }

    int status = 0;

    while (waitpid(pid, &status, 0) == -1)
    {
        if (errno == EINTR)
        {
            continue;
        }

        result.error = "waitpid: ";
        result.error += std::strerror(errno);
        result.error += '\n';
        break;
    }

    return result;
}

CommandResult Shell::execute(
    const std::string &input,
    bool capture_external_output)
{
    CommandResult result;
    std::vector<std::string> args;

    std::string parseError;

    if (!parseArguments(input, args, parseError))
    {
        result.error = parseError;
        return result;
    }

    if (args.empty())
    {
        return result;
    }

    const std::string &command = args[0];

    if (command == "exit")
    {
        result.should_exit = true;
        return result;
    }

    if (command == "echo")
    {
        for (std::size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1)
            {
                result.output += ' ';
            }

            result.output += args[i];
        }

        result.output += '\n';
        return result;
    }

    if (command == "pwd")
    {
        std::error_code ec;
        const auto cwd = std::filesystem::current_path(ec);

        if (ec)
        {
            result.error = "pwd: ";
            result.error += ec.message();
            result.error += '\n';
        }
        else
        {
            result.output = cwd.string() + '\n';
        }

        return result;
    }

    if (command == "cd")
    {
        if (args.size() > 2)
        {
            result.error = "cd: too many arguments\n";
            return result;
        }

        std::string path;

        if (args.size() == 1 || args[1] == "~")
        {
            const char *home = std::getenv("HOME");

            if (home == nullptr)
            {
                result.error = "cd: HOME not set\n";
                return result;
            }

            path = home;
        }
        else
        {
            path = args[1];

            if (path.rfind("~/", 0) == 0)
            {
                const char *home = std::getenv("HOME");

                if (home == nullptr)
                {
                    result.error = "cd: HOME not set\n";
                    return result;
                }

                path = std::string(home) + path.substr(1);
            }
        }

        if (chdir(path.c_str()) == -1)
        {
            result.error = "cd: ";
            result.error += path;
            result.error += ": ";
            result.error += std::strerror(errno);
            result.error += '\n';
        }

        return result;
    }

    if (command == "type")
    {
        if (args.size() < 2)
        {
            result.error = "type: missing operand\n";
            return result;
        }

        const std::string &target = args[1];

        if (
            target == "echo" ||
            target == "type" ||
            target == "exit" ||
            target == "pwd" ||
            target == "cd")
        {
            result.output =
                target + " is a shell builtin\n";
            return result;
        }

        const std::string executable = findExecutable(target);

        if (!executable.empty())
        {
            result.output =
                target + " is " + executable + '\n';
        }
        else
        {
            result.output = target + ": not found\n";
        }

        return result;
    }

    return executeExternal(args, capture_external_output);
}
