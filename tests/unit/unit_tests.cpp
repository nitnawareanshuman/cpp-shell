#include "check.hpp"
#include "shell/completion.hpp"
#include "shell/expander.hpp"
#include "shell/history.hpp"
#include "shell/lexer.hpp"
#include "shell/parser.hpp"
#include "shell/path.hpp"
#include "shell/state.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir() {
  const auto path =
      fs::temp_directory_path() /
      ("cpp-shell-unit-" + std::to_string(::getpid()) + "-XXXXXX");
  std::error_code ec;
  fs::create_directories(path, ec);
  return path;
}

void test_lexer_quoting() {
  auto lexed = shell::tokenize("echo hello");
  CHECK(lexed.error.empty());
  CHECK_EQ(lexed.tokens[0].kind, shell::TokenKind::Word);

  lexed = shell::tokenize("echo 'a b'");
  CHECK(lexed.error.empty());
  auto parsed = shell::parse("echo 'a b'");
  CHECK(parsed.error.empty());
  CHECK_EQ(parsed.pipeline.commands.size(), 1u);

  lexed = shell::tokenize("echo \"unclosed");
  CHECK(!lexed.error.empty());

  lexed = shell::tokenize("echo 'unclosed");
  CHECK(!lexed.error.empty());

  parsed = shell::parse(R"(echo "hello'world")");
  CHECK(parsed.error.empty());
}

void test_parser_operators() {
  auto parsed = shell::parse("echo a | cat | wc");
  CHECK(parsed.error.empty());
  CHECK_EQ(parsed.pipeline.commands.size(), 3u);

  parsed = shell::parse("echo hello > out.txt");
  CHECK(parsed.error.empty());
  CHECK_EQ(parsed.pipeline.commands[0].redirections.size(), 1u);
  CHECK(parsed.pipeline.commands[0].redirections[0].kind ==
        shell::RedirectKind::Out);

  parsed = shell::parse("echo hello >> out.txt");
  CHECK(parsed.pipeline.commands[0].redirections[0].kind ==
        shell::RedirectKind::Append);

  parsed = shell::parse("cat < in.txt");
  CHECK(parsed.pipeline.commands[0].redirections[0].kind ==
        shell::RedirectKind::In);

  parsed = shell::parse("echo hi 2> err.txt");
  CHECK(parsed.pipeline.commands[0].redirections[0].kind ==
        shell::RedirectKind::Err);

  parsed = shell::parse("echo hi 2>> err.txt");
  CHECK(parsed.pipeline.commands[0].redirections[0].kind ==
        shell::RedirectKind::ErrAppend);

  parsed = shell::parse("echo hi > out.txt 2>&1");
  CHECK_EQ(parsed.pipeline.commands[0].redirections.size(), 2u);
  CHECK(parsed.pipeline.commands[0].redirections[1].kind ==
        shell::RedirectKind::ErrToOut);

  parsed = shell::parse("sleep 1 &");
  CHECK(parsed.pipeline.background);

  parsed = shell::parse("| cat");
  CHECK(!parsed.error.empty());

  parsed = shell::parse("echo |");
  CHECK(!parsed.error.empty());

  parsed = shell::parse("echo >");
  CHECK(!parsed.error.empty());
}

void test_expansion() {
  const auto tmp = make_temp_dir();
  ::setenv("CPP_SHELL_HISTFILE", (tmp / "hist").c_str(), 1);
  ::setenv("CPP_SHELL_COMPLETIONS", (tmp / "comp.json").c_str(), 1);
  ::setenv("UNIT_VAR", "alpha beta", 1);
  ::setenv("EMPTY_VAR", "", 1);

  shell::ShellState state;
  auto word = shell::tokenize("echo $UNIT_VAR").tokens[1].word;
  auto expanded = shell::expand_word(word, state);
  CHECK_EQ(expanded.fields.size(), 2u);
  CHECK_EQ(expanded.fields[0], std::string("alpha"));
  CHECK_EQ(expanded.fields[1], std::string("beta"));

  word = shell::tokenize("echo \"$UNIT_VAR\"").tokens[1].word;
  expanded = shell::expand_word(word, state);
  CHECK_EQ(expanded.fields.size(), 1u);
  CHECK_EQ(expanded.fields[0], std::string("alpha beta"));

  word = shell::tokenize("echo '$UNIT_VAR'").tokens[1].word;
  expanded = shell::expand_word(word, state);
  CHECK_EQ(expanded.fields[0], std::string("$UNIT_VAR"));

  word = shell::tokenize("echo ${UNIT_VAR}").tokens[1].word;
  expanded = shell::expand_word(word, state);
  CHECK_EQ(expanded.fields.size(), 2u);

  word = shell::tokenize("echo $?").tokens[1].word;
  expanded = shell::expand_word(word, state);
  CHECK_EQ(expanded.fields[0], std::string("0"));

  word = shell::tokenize("echo ~/file").tokens[1].word;
  expanded = shell::expand_word(word, state);
  CHECK(expanded.fields[0].find("file") != std::string::npos);
  CHECK(expanded.fields[0][0] != '~');

  fs::remove_all(tmp);
}

void test_history_persistence() {
  const auto tmp = make_temp_dir();
  const auto histfile = tmp / "history";
  {
    shell::History history;
    history.set_path(histfile.string());
    CHECK(history.add("echo one"));
    CHECK(!history.add("echo one"));
    CHECK(history.add("echo two"));
    CHECK(!history.add("   "));
  }
  {
    shell::History history;
    history.set_path(histfile.string());
    history.load();
    CHECK_EQ(history.entries().size(), 2u);
    CHECK_EQ(history.entries()[0], std::string("echo one"));
    CHECK_EQ(history.up(""), std::string("echo two"));
    CHECK_EQ(history.up(""), std::string("echo one"));
    CHECK_EQ(history.down(""), std::string("echo two"));
  }
  fs::remove_all(tmp);
}

void test_programmable_completion() {
  const auto tmp = make_temp_dir();
  const auto cfg = tmp / "comp.json";
  {
    std::ofstream out(cfg);
    out << R"({
      "git": {
        "args": { "1": ["add", "commit", "status"] },
        "files": false
      }
    })";
  }
  ::setenv("CPP_SHELL_HISTFILE", (tmp / "hist").c_str(), 1);
  ::setenv("CPP_SHELL_COMPLETIONS", cfg.c_str(), 1);
  shell::ShellState state;
  auto err = state.completer().load_file(cfg.string());
  CHECK(err.empty());
  auto result = state.completer().complete("git com", 7);
  CHECK(!result.matches.empty());
  bool found_commit = false;
  for (const auto &match : result.matches) {
    if (match.find("commit") != std::string::npos) {
      found_commit = true;
    }
  }
  CHECK(found_commit);

  {
    std::ofstream out(cfg);
    out << "{ not json";
  }
  err = state.completer().load_file(cfg.string());
  CHECK(!err.empty());

  CHECK(!shell::quote_for_shell("file with spaces").empty());
  CHECK_EQ(shell::quote_for_shell("plain"), std::string("plain"));
  fs::remove_all(tmp);
}

} // namespace

int main() {
  test_lexer_quoting();
  test_parser_operators();
  test_expansion();
  test_history_persistence();
  test_programmable_completion();
  return test_summary();
}
