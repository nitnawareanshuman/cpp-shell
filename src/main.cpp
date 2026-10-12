#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

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

    if (input == "exit")
    {
      break;
    }
    else if (input.substr(0, 5) == "type ")
    {
      std::string command = input.substr(5);

      if (command == "echo" || command == "type" || command == "exit")
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
    else if (input.substr(0, 5) == "echo ")
    {
      std::cout << input.substr(5) << '\n';
    }
    else
    {
      std::cout << input << ": command not found\n";
    }
  }

  return 0;
}