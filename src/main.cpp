#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <unistd.h>
#include <filesystem>
#include <system_error>

#include "ui/desktop_ui.hpp"
#include "command_executor.hpp"

    namespace
{

  // Parse input into arguments while supporting single quotes,
  // double quotes, and backslash escaping.
  bool parseArguments(
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
      char ch = input[i];

      // Backslash handling inside double quotes.
      if (ch == '\\' && inDoubleQuotes)
      {
        if (i + 1 < input.size())
        {
          char next = input[i + 1];

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

      // Backslash escaping outside quotes.
      else if (ch == '\\' && !inSingleQuotes && !inDoubleQuotes)
      {
        wordStarted = true;

        if (i + 1 < input.size())
        {
          word += input[++i];
        }
        else
        {
          word += ch;
        }
      }

      // Single quotes.
      else if (ch == '\'' && !inDoubleQuotes)
      {
        inSingleQuotes = !inSingleQuotes;
        wordStarted = true;
      }

      // Double quotes.
      else if (ch == '"' && !inSingleQuotes)
      {
        inDoubleQuotes = !inDoubleQuotes;
        wordStarted = true;
      }

      // Whitespace separates arguments only outside quotes.
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

  // Check whether a command is one of our implemented built-ins.
  bool isBuiltin(const std::string &command)
  {
    return command == "exit" ||
           command == "echo" ||
           command == "pwd" ||
           command == "cd" ||
           command == "type";
  }

  // Find an executable using PATH without executing it.
  bool findExecutable(
      const std::string &command,
      std::string &executablePath)
  {
    const char *pathvar = std::getenv("PATH");

    if (pathvar == nullptr)
    {
      return false;
    }

    std::istringstream pathStream(pathvar);
    std::string directory;

    while (std::getline(pathStream, directory, ':'))
    {
      // An empty PATH entry represents the current directory.
      if (directory.empty())
      {
        directory = ".";
      }

      std::string candidate =
          (std::filesystem::path(directory) / command).string();

      if (access(candidate.c_str(), X_OK) == 0)
      {
        executablePath = candidate;
        return true;
      }
    }

    return false;
  }

} // namespace

// Shared command execution logic.
// Used by the CLI, terminal UI, and desktop UI.
CommandResult executeCommand(const std::string &input)
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

  // exit
  if (command == "exit")
  {
    if (args.size() > 2)
    {
      result.error = "exit: too many arguments\n";
      return result;
    }

    result.should_exit = true;
    return result;
  }

  // echo
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

  // pwd
  if (command == "pwd")
  {
    if (args.size() > 1)
    {
      result.error = "pwd: too many arguments\n";
      return result;
    }

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);

    if (ec)
    {
      result.error = "pwd: " + ec.message() + "\n";
    }
    else
    {
      result.output = cwd.string() + '\n';
    }

    return result;
  }

  // cd
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

      // Expand ~/ at the beginning of the path.
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

    if (chdir(path.c_str()) != 0)
    {
      result.error = "cd: " + path + ": directory change failed\n";
    }

    return result;
  }

  // type
  if (command == "type")
  {
    if (args.size() != 2)
    {
      result.error = "type: expected one argument\n";
      return result;
    }

    const std::string &target = args[1];

    if (isBuiltin(target))
    {
      result.output = target + " is a shell builtin\n";
      return result;
    }

    std::string executablePath;

    if (findExecutable(target, executablePath))
    {
      result.output = target + " is " + executablePath + "\n";
    }
    else
    {
      result.error = target + ": not found\n";
    }

    return result;
  }

  // Unknown commands are never executed.
  result.error = command + ": command not implemented\n";
  return result;
}

// Application entry point.
int main(int argc, char *argv[])
{
  // Desktop GUI.
  if (argc == 2 && std::string(argv[1]) == "--ui")
  {
    return runDesktopUI(argc, argv);
  }

  // Default: regular command-line shell.
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true)
  {
    std::cout << "$ ";

    std::string input;

    if (!std::getline(std::cin, input))
    {
      break;
    }

    CommandResult result = executeCommand(input);

    if (!result.output.empty())
    {
      std::cout << result.output;
    }

    if (!result.error.empty())
    {
      std::cerr << result.error;
    }

    if (result.should_exit)
    {
      break;
    }
  }

  return 0;
}