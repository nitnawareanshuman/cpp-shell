#include "desktop_ui.hpp"
#include "command_executor.hpp"

#include <QApplication>
#include <QFont>
#include <QMainWindow>
#include <QTextEdit>
#include <QTextCursor>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPalette>
#include <QWidget>

#include <string>

    class TerminalWidget : public QTextEdit
{
public:
    explicit TerminalWidget(QWidget *parent = nullptr)
        : QTextEdit(parent)
    {
        setReadOnly(false);
        setAcceptRichText(false);
        setUndoRedoEnabled(false);
        setFrameShape(QFrame::NoFrame);

        QFont font("Menlo", 13);
        font.setStyleHint(QFont::Monospace);
        setFont(font);

        setStyleSheet(
            "QTextEdit {"
            " background-color: #080b16;"
            " color: #e5e7eb;"
            " border: none;"
            " padding: 22px;"
            " selection-background-color: #164e63;"
            " selection-color: white;"
            "}"
            "QScrollBar:vertical {"
            " background: transparent;"
            " width: 7px;"
            "}"
            "QScrollBar::handle:vertical {"
            " background: #334155;"
            " border-radius: 3px;"
            "}");

        document()->setDefaultStyleSheet(
            "span.prompt { color: #34d399; }"
            "span.command { color: #22d3ee; }"
            "span.error { color: #f87171; }");

        insertHtml(
            "<span style='color:#34d399;'>Welcome to shell</span>"
            "<br><span style='color:#94a3b8;'>"
            "Type 'exit' to quit.</span><br><br>");

        insertPrompt();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        // Keep command editing on the current prompt line.
        QTextCursor cursor = textCursor();
        const int promptPosition = document()->property("promptPosition").toInt();

        if (event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Enter)
        {
            if (cursor.position() < promptPosition)
                return;

            QTextCursor endCursor(document());
            endCursor.movePosition(QTextCursor::End);
            setTextCursor(endCursor);

            QString command = document()->toPlainText().mid(promptPosition).trimmed();

            // The prompt itself is inserted as text for editing.
            // Extract only the command after the latest prompt.
            executeLine(command);
            return;
        }

        if (event->key() == Qt::Key_Backspace &&
            cursor.position() <= promptPosition)
        {
            return;
        }

        if (event->key() == Qt::Key_Left &&
            cursor.position() <= promptPosition)
        {
            return;
        }

        if (event->key() == Qt::Key_Up ||
            event->key() == Qt::Key_Down)
        {
            return;
        }

        QTextEdit::keyPressEvent(event);
    }

private:
    void insertPrompt()
    {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);

        QTextCharFormat promptFormat;
        promptFormat.setForeground(QColor("#34d399"));

        cursor.insertText("~ ❯ ", promptFormat);
        document()->setProperty(
            "promptPosition", cursor.position());

        setTextCursor(cursor);
        ensureCursorVisible();
    }

    void executeLine(const QString &command)
    {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);

        // Start output on the next line.
        insertPlainText("\n");

        if (command.isEmpty())
        {
            insertPrompt();
            return;
        }

        const CommandResult result =
            ::executeCommand(command.toStdString());

        if (!result.output.empty())
        {
            insertPlainText(
                QString::fromStdString(result.output));

            if (!result.output.ends_with('\n'))
                insertPlainText("\n");
        }

        if (!result.error.empty())
        {
            QTextCharFormat errorFormat;
            errorFormat.setForeground(QColor("#f87171"));

            cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(
                QString::fromStdString(result.error),
                errorFormat);
            insertPlainText("\n");
        }

        if (result.should_exit)
        {
            window()->close();
            return;
        }

        insertPrompt();
    }
};

int runDesktopUI(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("shell");

    QMainWindow window;
    window.setWindowTitle("shell");
    window.resize(1100, 700);
    window.setMinimumSize(650, 400);

    auto *terminal = new TerminalWidget(&window);
    window.setCentralWidget(terminal);

    window.show();
    terminal->setFocus();

    return app.exec();
}