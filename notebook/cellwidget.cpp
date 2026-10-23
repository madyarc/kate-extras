#include "cellwidget.h"
#include "outputview.h"

#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace
{
constexpr int s_selectionBarWidth = 3;

// Kate's highlighting definition for a notebook language ("python", "r", "julia", ...)
QString highlightingFor(const QString &language)
{
    static const QHash<QString, QString> modes{
        {QStringLiteral("python"), QStringLiteral("Python")},
        {QStringLiteral("r"), QStringLiteral("R Script")},
        {QStringLiteral("julia"), QStringLiteral("Julia")},
        {QStringLiteral("c++"), QStringLiteral("C++")},
        {QStringLiteral("javascript"), QStringLiteral("JavaScript")},
        {QStringLiteral("typescript"), QStringLiteral("TypeScript")},
        {QStringLiteral("bash"), QStringLiteral("Bash")},
        {QStringLiteral("sql"), QStringLiteral("SQL")},
    };
    return modes.value(language.toLower(), QStringLiteral("Python"));
}

bool isReturn(const QKeyEvent *event)
{
    return event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
}
}

CellWidget::CellWidget(const NotebookCell &cell, const QString &language, const QString &notebookDir, QScrollArea *scrollArea, QWidget *parent)
    : QFrame(parent)
    , m_cell(cell)
    , m_language(language)
    , m_notebookDir(notebookDir)
    , m_token(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_executionCount(cell.executionCount)
    , m_scrollArea(scrollArea)
    , m_prompt(new QLabel(this))
    , m_document(KTextEditor::Editor::instance()->createDocument(this))
    , m_view(nullptr)
    , m_rendered(new AutoHeightBrowser(this))
    , m_output(new OutputView(this))
    , m_rewrapTimer(new QTimer(this))
{
    m_document->setText(cell.source);
    m_view = m_document->createView(this);
    m_view->setStatusBarEnabled(false);
    m_view->setConfigValue(QStringLiteral("line-numbers"), false);
    m_view->setConfigValue(QStringLiteral("icon-bar"), false);
    m_view->setConfigValue(QStringLiteral("folding-bar"), false);
    m_view->setConfigValue(QStringLiteral("scrollbar-minimap"), false);
    m_view->setConfigValue(QStringLiteral("scrollbar-preview"), false);
    m_view->setConfigValue(QStringLiteral("modification-markers"), false);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Lines wrap (so Kate drops the horizontal scroll bar) and the editor is always as
    // tall as its text, so the vertical scroll bar is dead weight too
    m_view->setConfigValue(QStringLiteral("dynamic-word-wrap"), true);
    const auto scrollBars = m_view->findChildren<QScrollBar *>();
    for (QScrollBar *bar : scrollBars) {
        if (bar->orientation() == Qt::Vertical) {
            bar->hide();
            bar->installEventFilter(this);
        }
    }
    m_view->installEventFilter(this);
    if (m_view->focusProxy()) {
        m_view->focusProxy()->installEventFilter(this);
    }

    m_prompt->setFont(KTextEditor::Editor::instance()->font());
    m_prompt->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_prompt->setMinimumWidth(QFontMetrics(m_prompt->font()).horizontalAdvance(QStringLiteral("[000]:")));
    m_prompt->setEnabled(false);

    m_rendered->viewport()->installEventFilter(this);
    m_rendered->setToolTip(i18n("Double-click to edit"));
    m_rendered->document()->setBaseUrl(QUrl::fromLocalFile(notebookDir + QLatin1Char('/')));
    m_rendered->setSearchPaths({notebookDir});

    auto *body = new QVBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);
    body->addWidget(m_view);
    body->addWidget(m_rendered);
    body->addWidget(m_output);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(s_selectionBarWidth + 6, 4, 6, 4);
    layout->addWidget(m_prompt);
    layout->addLayout(body, 1);

    connect(m_document, &KTextEditor::Document::textChanged, this, [this]() {
        updateEditorHeight();
        Q_EMIT changed();
    });
    connect(m_view, &KTextEditor::View::focusIn, this, [this]() {
        Q_EMIT activated(this);
    });

    m_rewrapTimer->setSingleShot(true);
    m_rewrapTimer->setInterval(50);
    connect(m_rewrapTimer, &QTimer::timeout, this, [this]() {
        m_view->setConfigValue(QStringLiteral("dynamic-word-wrap"), false);
        m_view->setConfigValue(QStringLiteral("dynamic-word-wrap"), true);
        updateEditorHeight();
    });

    m_output->setOutputs(cell.outputs);
    applyType();
    // Markdown with content opens rendered; empty markdown is something to write
    setEditing(!isText() || cell.source.trimmed().isEmpty());
}

NotebookCell CellWidget::cell() const
{
    NotebookCell cell = m_cell;
    cell.source = m_document->text();
    if (cell.type == QLatin1String("code")) {
        cell.outputs = m_output->outputs();
        cell.executionCount = m_executionCount;
    } else {
        cell.outputs = QJsonArray();
        cell.executionCount = QJsonValue::Null;
    }
    return cell;
}

QString CellWidget::source() const
{
    return m_document->text();
}

QString CellWidget::type() const
{
    return m_cell.type;
}

void CellWidget::setType(const QString &type)
{
    if (type == m_cell.type) {
        return;
    }
    m_cell.type = type;
    if (type != QLatin1String("code")) {
        m_output->clear();
        m_executionCount = QJsonValue::Null;
    }
    applyType();
    setEditing(true);
    Q_EMIT changed();
}

QString CellWidget::token() const
{
    return m_token;
}

void CellWidget::setSelected(bool selected)
{
    if (selected != m_selected) {
        m_selected = selected;
        update();
    }
}

void CellWidget::focusEditor()
{
    setEditing(true);
    m_view->setFocus();
    m_scrollArea->ensureWidgetVisible(this, 0, 40);
}

void CellWidget::render()
{
    if (isText()) {
        setEditing(false);
    }
}

void CellWidget::setQueued()
{
    updatePrompt(QStringLiteral("[*]:"));
}

void CellWidget::setExecutionCount(const QJsonValue &count)
{
    m_executionCount = count;
    updatePrompt({});
    Q_EMIT changed();
}

void CellWidget::cancelQueued()
{
    updatePrompt({});
}

OutputView *CellWidget::outputView() const
{
    return m_output;
}

bool CellWidget::isText() const
{
    return m_cell.type != QLatin1String("code");
}

void CellWidget::applyType()
{
    const bool markdown = m_cell.type == QLatin1String("markdown");
    m_document->setHighlightingMode(markdown ? QStringLiteral("Markdown") : isText() ? QStringLiteral("None") : highlightingFor(m_language));
    m_prompt->setVisible(!isText());
    m_output->setVisible(!isText() && !m_output->outputs().isEmpty());
    updatePrompt({});
    updateEditorHeight();
}

void CellWidget::setEditing(bool editing)
{
    m_editing = editing || !isText();
    if (!m_editing) {
        if (m_cell.type == QLatin1String("markdown")) {
            // Pasted images live in the cell as attachment:name.png
            const QJsonObject attachments = m_cell.extra.value(QLatin1String("attachments")).toObject();
            for (auto it = attachments.begin(); it != attachments.end(); ++it) {
                const QJsonObject bundle = it->toObject();
                for (auto mime = bundle.begin(); mime != bundle.end(); ++mime) {
                    QImage image;
                    if (image.loadFromData(QByteArray::fromBase64(mime->toString().toLatin1()))) {
                        m_rendered->document()->addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("attachment:") + it.key()), image);
                        break;
                    }
                }
            }
            m_rendered->setMarkdown(m_document->text());
        } else {
            m_rendered->setPlainText(m_document->text());
        }
    }
    m_view->setVisible(m_editing);
    m_rendered->setVisible(!m_editing);
    updateEditorHeight();
}

void CellWidget::updateEditorHeight()
{
    if (!m_editing) {
        return;
    }

    // First a guess from the line count, so the cell does not jump around while Kate lays
    // out the text; then the exact height, from where Kate actually drew the last line
    static int lineHeight = 0;
    const int line = lineHeight > 0 ? lineHeight : QFontMetrics(KTextEditor::Editor::instance()->font()).height() + 1;
    int height = m_view->height();
    if (height <= 1 || m_document->lines() != m_measuredLines) {
        height = 3 + m_document->lines() * line;
    }

    const QPoint first = m_view->cursorToCoordinate(KTextEditor::Cursor(0, 0));
    const QPoint last = m_view->cursorToCoordinate(m_document->documentEnd());
    if (first.y() >= 0 && last.y() >= 0 && m_view->firstDisplayedLine() == 0) {
        if (m_document->lines() >= 2 && last.y() > first.y()) {
            // Spacing between screen lines, measured once for the whole notebook
            const QPoint second = m_view->cursorToCoordinate(KTextEditor::Cursor(1, 0));
            if (second.y() > first.y()) {
                lineHeight = second.y() - first.y();
            }
        }
        height = last.y() + line + first.y() + 1;
        m_measuredLines = m_document->lines();
    } else if (last.y() < 0) {
        // The end is below the bottom edge: grow, and measure again once Kate has relaid out
        height = qMax(height, m_view->height() + line);
        QTimer::singleShot(0, this, &CellWidget::updateEditorHeight);
    }

    if (height != m_view->height()) {
        m_view->setFixedHeight(height);
        QTimer::singleShot(0, this, &CellWidget::updateEditorHeight);
    }
    // Typing past the old bottom edge scrolls the view before it grows; scroll it back
    if (m_view->firstDisplayedLine() > 0) {
        QTimer::singleShot(0, m_view, [view = m_view]() {
            view->setScrollPosition(KTextEditor::Cursor(0, 0));
        });
    }
}

void CellWidget::updatePrompt(const QString &text)
{
    if (!text.isEmpty()) {
        m_prompt->setText(text);
        return;
    }
    m_prompt->setText(m_executionCount.isDouble() ? QStringLiteral("[%1]:").arg(m_executionCount.toInt()) : QStringLiteral("[ ]:"));
}

bool CellWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Kate shows its scroll bar again whenever it relayouts
    if (event->type() == QEvent::Show) {
        if (auto *bar = qobject_cast<QScrollBar *>(watched)) {
            QTimer::singleShot(0, bar, &QWidget::hide);
        }
        return false;
    }

    if (watched == m_view && event->type() == QEvent::Resize) {
        // Kate does not re-wrap a one-line view when it gets narrower; once the width
        // settles, switch wrapping off and on to make it lay the text out again
        auto *resize = static_cast<QResizeEvent *>(event);
        if (resize->size().width() != resize->oldSize().width()) {
            m_rewrapTimer->start();
        } else {
            QTimer::singleShot(0, this, &CellWidget::updateEditorHeight);
        }
        return false;
    }

    if (watched == m_rendered->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        focusEditor();
        return true;
    }
    if (watched == m_rendered->viewport() && event->type() == QEvent::MouseButtonPress) {
        Q_EMIT activated(this);
        return false;
    }

    if (watched != m_view->focusProxy()) {
        return false;
    }

    switch (event->type()) {
    case QEvent::Wheel: {
        // The editor never scrolls vertically, so let the notebook scroll instead
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (qAbs(wheel->angleDelta().y()) >= qAbs(wheel->angleDelta().x())) {
            QApplication::sendEvent(m_scrollArea->verticalScrollBar(), event);
            return true;
        }
        return false;
    }
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
        // On macOS, Qt's ControlModifier is Cmd and MetaModifier is Ctrl; accept both
        const bool runKey = isReturn(key) && modifiers != Qt::NoModifier;
        // Kate's editor would save or "save as" the cell on its own otherwise
        const bool saveKey = key->matches(QKeySequence::Save) || key->matches(QKeySequence::SaveAs);
        if (!runKey && !saveKey) {
            return false;
        }
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (saveKey) {
            Q_EMIT saveRequested();
        } else if (modifiers == Qt::ShiftModifier) {
            Q_EMIT runRequested(this, RunAndAdvance);
        } else if (modifiers == Qt::AltModifier) {
            Q_EMIT runRequested(this, RunAndInsert);
        } else {
            Q_EMIT runRequested(this, RunInPlace);
        }
        return true;
    }
    default:
        return false;
    }
}

void CellWidget::mousePressEvent(QMouseEvent *event)
{
    Q_EMIT activated(this);
    QFrame::mousePressEvent(event);
}

void CellWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    if (!m_selected) {
        return;
    }
    QPainter painter(this);
    painter.fillRect(QRect(0, 0, s_selectionBarWidth, height()), palette().color(QPalette::Highlight));
}
