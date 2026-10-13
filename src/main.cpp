#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <cstdio>
#include <sys/wait.h>
#include <filesystem>
#include <cerrno>
#include <cctype>

bool parseArguments(const std::string &input, std::vector<std::string> &args)
{
  std::string word;
  bool inSingleQuotes = false;
  bool inDoubleQuotes = false;
  bool wordStarted = false;

  for (char ch : input)
  {
    if (ch == '\'' && !inDoubleQuotes)
    {
      // Change quote state without adding the quote to the argument.
      inSingleQuotes = !inSingleQuotes;
      wordStarted = true;
    }
    else if (ch == '"' && !inSingleQuotes)
    {
      //Handle double quotes outside single quotes
      inDoubleQuotes = !inDoubleQuotes;
      wordStarted = true;
    }
    else if (std::isspace(static_cast<unsigned char>(ch)) && !inSingleQuotes && !inDoubleQuotes)
    {
      // Only whitespace outside quotes separates arguments.
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
    std::cerr << "Unclosed single quote\n";
    return false;
  }
  if (inDoubleQuotes)
  {
    std::cerr << "Unclosed double quote\n";
  }

  if (wordStarted)
  {
    args.push_back(word);
  }

  return true;
}

int main()
{
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

    std::vector<std::string> args;

    if (!parseArguments(input, args) || args.empty())
    {
      continue;
    }

    if (input == "exit") // exit command
    {
      break;
    }
    else if (input.substr(0, 5) == "type ") // type command
    {
      std::string command = input.substr(5);

      if (command == "echo" || command == "type" || 
          command == "exit" || command == "pwd" ||
          command == "cd")
      {
        std::cout << command << " is a shell builtin\n";
        continue;
      }

      bool found = false;
      const char *pathvar = std::getenv("PATH");

      if (pathvar != nullptr)
      {
        std::istringstream path_stream(pathvar);
        std::string directory;

        while (std::getline(path_stream, directory, ':'))
        {
          // An empty PATH entry represents the current directory.
          if (directory.empty())
          {
            directory = ".";
          }

          std::string filepath = directory + "/" + command;

          if (access(filepath.c_str(), X_OK) == 0)
          {
            std::cout << command << " is " << filepath << '\n';
            found = true;
            break;
          }
        }
      }

      if (!found)
      {
        std::cout << command << ": not found\n";
      }
    }
    else if (args[0] == "echo") // echo command
    {
      for (std::size_t i = 1; i < args.size(); ++i)
      {
        if (i > 1)
        {
          std::cout << ' ';
        }

        std::cout << args[i];
      }

      std::cout << '\n';
    }
    else if (input == "pwd") // pwd command
    {
      char* cwd = getcwd(nullptr, 0);

      if (cwd == nullptr)
      {
        std::perror("pwd");
      }
      else
      {
        std::cout << cwd << std::endl;
        std::free(cwd);
      }
    }
    else if (input == "cd" || input.substr(0, 3) == "cd ") // cd command
    {
      std::string path = (input == "cd") ? "~" : input.substr(3);

      if (path == "~" || path.substr(0, 2) == "~/")
      {
        const char *home = std::getenv("HOME");

        if (home == nullptr)
        {
          std::cerr << "cd: HOME not set\n";
          continue;
        }

        path = std::string(home) + path.substr(1);
      }

      // Change directory only once.
      if (chdir(path.c_str()) == -1)
      {
        std::perror(("cd: " + path).c_str());
      }
    }

    else
    {
      // Use the arguments already parsed above.
      std::vector<char *> argv;

      for (std::string &arg : args)
      {
        argv.push_back(arg.data());
      }
      
      argv.push_back(nullptr);

      pid_t pid = fork();

      if (pid == -1)
      {
        // The child process could not be created.
        std::perror("fork");
      }
      else if (pid == 0)
      {
        // Child process: find and execute the program.
        execvp(argv[0], argv.data());

        // This code runs only if execvp fails.
        int error = errno;

        if (error == ENOENT)
        {
          std::cerr << args[0] << ": command not found\n";
        }
        else
        {
          errno = error;
          std::perror(args[0].c_str());
        }

        _exit(error == ENOENT ? 127 : 126);
      }
      else
      {
        // Parent process: wait for the child to finish.
        while (waitpid(pid, nullptr, 0) == -1)
        {
          if (errno != EINTR)
          {
            std::perror("waitpid");
            break;
          }
        }
      }
    }
  }

  return 0;
}