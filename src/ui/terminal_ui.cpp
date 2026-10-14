
#include "terminal_ui.hpp"
#include "shell/shell.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace
{
    using namespace ftxui;

    void appendLines(
        std::vector<std::string> &history,
        const std::string &text)
    {
        std::size_t start = 0;

        while (start < text.size())
        {
            const std::size_t end = text.find('\n', start);

            if (end == std::string::npos)
            {
                history.push_back(text.substr(start));
                return;
            }

            history.push_back(text.substr(start, end - start));
            start = end + 1;
        }

        if (!text.empty() && text.back() == '\n')
        {
            // The trailing newline terminates the previous line.
            // Do not add an unnecessary extra history entry.
        }
    }

    void trimHistory(std::vector<std::string> &history)
    {
        constexpr std::size_t maxLines = 200;

        if (history.size() > maxLines)
        {
            history.erase(
                history.begin(),
                history.begin() +
                    static_cast<std::ptrdiff_t>(
                        history.size() - maxLines));
        }
    }
} // namespace

int runTerminalUI()
{
    using namespace ftxui;

    Shell shell;
    std::string command;
    std::vector<std::string> history;

    auto screen = ScreenInteractive::Fullscreen();

    InputOption inputOptions;
    inputOptions.on_enter = [&]()
    {
        if (command.empty())
        {
            return;
        }

        history.push_back("$ " + command);

        const CommandResult result = shell.execute(command, true);

        appendLines(history, result.output);
        appendLines(history, result.error);
        trimHistory(history);

        command.clear();

        if (result.should_exit)
        {
            screen.Exit();
        }
    };

    auto input = Input(
        &command,
        "Enter a shell command...",
        inputOptions);

    auto renderer = Renderer(input, [&]
                             {
        Elements outputElements;

        const std::size_t first =
            history.size() > 16 ? history.size() - 16 : 0;

        for (std::size_t i = first; i < history.size(); ++i)
        {
            outputElements.push_back(
                paragraph(history[i])
            );
        }

        if (outputElements.empty())
        {
            outputElements.push_back(
                text("Welcome to your C++ shell dashboard.") |
                color(Color::GrayDark)
            );

            outputElements.push_back(
                text("Try: help is not implemented; use pwd, ls, echo, type, or cd.") |
                color(Color::GrayDark)
            );
        }

        return vbox({
            hbox({
                text(" SHELL DASHBOARD ") |
                    bold |
                    color(Color::Cyan),
                filler(),
                text("FTXUI") | color(Color::Green),
            }),
            separator(),
            vbox(std::move(outputElements)) | flex,
            separator(),
            hbox({
                text(" $ ") | color(Color::Green) | bold,
                input->Render() | flex,
            }),
            separator(),
            text(" Enter: execute   |   Esc: quit dashboard ") |
                color(Color::GrayDark),
        }) |
        border |
        bgcolor(Color::Black); });

    renderer = CatchEvent(
        renderer,
        [&](Event event)
        {
            if (event == Event::Escape)
            {
                screen.Exit();
                return true;
            }

            return false;
        });

    screen.Loop(renderer);

    return 0;
}
