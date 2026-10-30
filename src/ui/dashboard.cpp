#include "shell/dashboard.hpp"
#include "shell/completion.hpp"
#include "shell/executor.hpp"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <string>
#include <utility>

namespace shell {
namespace {

class CommandInput final : public QLineEdit {
public:
  using KeyHandler = std::function<void(QKeyEvent *)>;

  explicit CommandInput(KeyHandler handler, QWidget *parent = nullptr)
      : QLineEdit(parent), key_handler_(std::move(handler)) {}

  void forwardKeyPressEvent(QKeyEvent *event) {
    QLineEdit::keyPressEvent(event);
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    key_handler_(event);
  }

private:
  KeyHandler key_handler_;
};

class DashboardWindow final : public QMainWindow {
public:
  explicit DashboardWindow(Shell &shell) : shell_(shell) {
    setWindowTitle("cpp-shell");
    resize(1100, 700);
    setMinimumSize(650, 400);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(" cpp-shell ", central);
    title->setStyleSheet(
        "QLabel { background: #2563eb; color: white; font-weight: bold; "
        "padding: 5px; }");
    status_ = new QLabel("ready", central);
    cwd_ = new QLabel(QString::fromStdString(shell_.state().cwd()), central);
    header->addWidget(title);
    header->addWidget(cwd_);
    header->addStretch();
    header->addWidget(status_);
    layout->addLayout(header);

    output_ = new QPlainTextEdit(central);
    output_->setReadOnly(true);
    output_->setFont(QFont("Menlo", 13));
    output_->setStyleSheet(
        "QPlainTextEdit { background: #080b16; color: #e5e7eb; "
        "border: none; padding: 8px; }");
    output_->appendPlainText("cpp-shell dashboard");
    output_->appendPlainText(
        "Tab completes, Up/Down history, Ctrl-L clears, Ctrl-D exits.");
    const std::string load_error = shell_.state().load_completions();
    if (!load_error.empty()) {
      output_->appendPlainText(QString::fromStdString(load_error));
    }
    layout->addWidget(output_, 1);

    auto *input_layout = new QHBoxLayout();
    auto *prompt = new QLabel("$", central);
    prompt->setStyleSheet("QLabel { color: #34d399; font-weight: bold; }");
    input_ = new CommandInput(
        [this](QKeyEvent *event) { handleInputKey(event); }, central);
    input_->setPlaceholderText("Enter a command");
    input_->setFont(QFont("Menlo", 13));
    input_->setStyleSheet(
        "QLineEdit { background: #111827; color: #e5e7eb; border: 1px solid "
        "#334155; padding: 8px; }");
    input_layout->addWidget(prompt);
    input_layout->addWidget(input_, 1);
    layout->addLayout(input_layout);

    jobs_ = new QLabel(central);
    jobs_->setStyleSheet("QLabel { color: #94a3b8; }");
    layout->addWidget(jobs_);

    setCentralWidget(central);
    refreshJobs();
    input_->setFocus();
  }

private:
  void handleInputKey(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      runLine(input_->text().trimmed().toStdString());
      return;
    }
    if (event->key() == Qt::Key_Tab) {
      const auto completed =
          shell_.state().completer().complete(input_->text().toStdString(),
                                              input_->cursorPosition());
      if (completed.applied) {
        input_->setText(QString::fromStdString(completed.line));
        input_->setCursorPosition(completed.cursor);
      } else if (!completed.display.empty()) {
        appendOutput(completed.display);
      }
      return;
    }
    if (event->key() == Qt::Key_Up) {
      const auto line = shell_.state().history().up(input_->text().toStdString());
      input_->setText(QString::fromStdString(line));
      input_->end(false);
      return;
    }
    if (event->key() == Qt::Key_Down) {
      const auto line =
          shell_.state().history().down(input_->text().toStdString());
      input_->setText(QString::fromStdString(line));
      input_->end(false);
      return;
    }
    if (event->key() == Qt::Key_L &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
      output_->clear();
      return;
    }
    if (event->key() == Qt::Key_D &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
      close();
      return;
    }
    input_->forwardKeyPressEvent(event);
  }

  void runLine(const std::string &command) {
    input_->clear();
    if (command.empty()) {
      return;
    }

    appendOutput("$ " + command);
    status_->setText("running");

    ExecOptions options;
    options.capture_output = true;
    options.interactive_tty = false;
    const ExecResult result = shell_.execute(command, options);
    if (!result.output.empty()) {
      appendOutput(result.output);
    }
    if (!result.error.empty()) {
      appendOutput(result.error);
    }
    status_->setText(
        QString("exit %1").arg(QString::number(result.exit_status)));
    cwd_->setText(QString::fromStdString(shell_.state().cwd()));
    refreshJobs();
    shell_.state().history().reset_cursor();

    if (result.should_exit) {
      close();
    }
  }

  void appendOutput(const std::string &text) {
    output_->appendPlainText(QString::fromStdString(text));
  }

  void refreshJobs() {
    const auto notices = shell_.state().jobs().reap();
    for (const auto &notice : notices) {
      appendOutput(notice);
    }
    const auto jobs = shell_.state().jobs().format_list();
    jobs_->setText(jobs.empty()
                       ? "Jobs: none"
                       : QString("Jobs: %1").arg(
                             QString::fromStdString(jobs.front())));
  }

  Shell &shell_;
  QPlainTextEdit *output_ = nullptr;
  CommandInput *input_ = nullptr;
  QLabel *status_ = nullptr;
  QLabel *cwd_ = nullptr;
  QLabel *jobs_ = nullptr;
};

} // namespace

int run_dashboard(Shell &shell) {
  int argc = 1;
  char application_name[] = "cpp-shell";
  char *argv[] = {application_name, nullptr};
  QApplication app(argc, argv);
  app.setApplicationName("cpp-shell");

  DashboardWindow window(shell);
  window.show();
  return app.exec();
}

} // namespace shell
