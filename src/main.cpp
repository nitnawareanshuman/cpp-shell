#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <cstdio>
#include <sys/wait.h>

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

    if (input == "exit") // exit command
    {
      break;
    }
    else if (input.substr(0, 5) == "type ") // type command
    {
      std::string command = input.substr(5);

      if (command == "echo" || command == "type" || command == "exit" || command == "pwd")
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
    else if (input.substr(0, 5) == "echo ") // echo command
    {
      std::cout << input.substr(5) << '\n';
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
    else
    {
      // Split input into the command name and its arguments.
      std::istringstream input_stream(input);
      std::vector<std::string> args;
      std::string word;

      while (input_stream >> word)
      {
        args.push_back(word);
      }

      // Ignore empty input.
      if (args.empty())
      {
        continue;
      }

      // execvp expects a null-terminated array of C-string pointers.
      std::vector<char *> argv;

      for (std::string &arg : args)
      {
        argv.push_back(arg.data()); // C++17 or later
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