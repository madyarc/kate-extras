#pragma once

#include "notebook.h"

#include <QFrame>

class AutoHeightBrowser;
class OutputView;
class QLabel;
class QScrollArea;
class QTimer;

namespace KTextEditor
{
class Document;
class View;
}

// One notebook cell: a Kate editor sized to its content, the run prompt, and the outputs.
// Markdown cells show their rendered text until double-clicked.
class CellWidget : public QFrame
{
    Q_OBJECT

public:
    enum RunMode {
        RunInPlace, // Ctrl/Cmd+Enter
        RunAndAdvance, // Shift+Enter
        RunAndInsert, // Alt/Option+Enter
    };

    CellWidget(const NotebookCell &cell, const QString &language, const QString &notebookDir, QScrollArea *scrollArea, QWidget *parent = nullptr);

    // The cell as it should be saved
    NotebookCell cell() const;
    QString source() const;
    QString type() const;
    void setType(const QString &type);

    // Identifies this cell in kernel requests; cell ids are optional in older notebooks
    QString token() const;

    void setSelected(bool selected);
    void focusEditor();
    // Markdown: leave editing and show the rendered text
    void render();

    void setQueued();
    void setExecutionCount(const QJsonValue &count);
    // Back to the last real execution count after a queued run was cancelled
    void cancelQueued();
    OutputView *outputView() const;

Q_SIGNALS:
    void activated(CellWidget *cell);
    void runRequested(CellWidget *cell, CellWidget::RunMode mode);
    void saveRequested();
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool isText() const; // markdown or raw: no prompt, no outputs
    void applyType();
    void setEditing(bool editing);
    void updateEditorHeight();
    void updatePrompt(const QString &text);

    NotebookCell m_cell; // type, extra and the saved state of everything else
    QString m_language;
    QString m_notebookDir;
    QString m_token;
    QJsonValue m_executionCount;
    bool m_selected = false;
    bool m_editing = true;
    int m_measuredLines = -1; // line count the editor height was last measured for

    QScrollArea *m_scrollArea;
    QLabel *m_prompt;
    KTextEditor::Document *m_document;
    KTextEditor::View *m_view;
    AutoHeightBrowser *m_rendered;
    OutputView *m_output;
    QTimer *m_rewrapTimer;
};
