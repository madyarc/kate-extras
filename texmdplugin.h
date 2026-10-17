// SPDX-FileCopyrightText: 2026 Madhesh
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KTextEditor/Plugin>

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>

namespace KTextEditor { class MainWindow; class View; class Document; }
namespace KParts { class ReadOnlyPart; }
namespace Okular { class ViewerInterface; }

class QAction;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QStackedWidget;
class QWebEngineView;
class QPushButton;

class TexMdPlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit TexMdPlugin(QObject *parent, const QVariantList & = QVariantList());
    QObject *createView(KTextEditor::MainWindow *mainWindow) override;
};

class TexMdView : public QObject
{
    Q_OBJECT
public:
    TexMdView(TexMdPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~TexMdView() override;

private Q_SLOTS:
    // inverse search: Shift+click in the PDF -> jump to source line
    void onSourceReference(const QString &absFileName, int line, int column);

private:
    enum class Kind { None, Markdown, Tex };

    void onViewChanged(KTextEditor::View *view);
    Kind kindOf(KTextEditor::Document *doc) const;

    // markdown
    void pushMarkdown();
    void onWebLoaded(bool ok);
    void syncMarkdownScroll(); // cursor line -> preview position
    int mdCursorLine() const;

    // latex
    void compile();
    void onCompileFinished(int exitCode);
    QString rootTexFile() const;
    QString magicComment(const QString &key) const;
    void showPdf(const QString &pdfPath, bool forceReload = false);
    bool ensurePart();
    void forwardSearch(); // cursor line -> PDF location
    QString pdfForRoot(const QString &root) const;

    KTextEditor::MainWindow *m_mw;
    QWidget *m_toolview = nullptr;
    QStackedWidget *m_stack = nullptr;

    QWidget *m_infoPage = nullptr;
    QLabel *m_infoLabel = nullptr;

    QWebEngineView *m_web = nullptr;
    bool m_webReady = false;
    QTimer m_mdTimer;

    QWidget *m_texPage = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_compileBtn = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QWidget *m_pdfHolder = nullptr;
    QPointer<KParts::ReadOnlyPart> m_part;
    Okular::ViewerInterface *m_viewer = nullptr; // same object as m_part, if it's Okular
    QAction *m_syncAction = nullptr;
    QTimer m_followTimer;            // debounce for cursor-follow
    int m_lastSyncedLine = -1;
    QLabel *m_pdfPlaceholder = nullptr;
    QProcess *m_proc = nullptr;
    bool m_recompileQueued = false;
    QString m_runningRoot;

    QPointer<KTextEditor::Document> m_doc;
    QPointer<KTextEditor::View> m_view;
    QList<QMetaObject::Connection> m_docConns;
    Kind m_kind = Kind::None;
};
