// SPDX-FileCopyrightText: 2026 Madhesh
// SPDX-License-Identifier: GPL-3.0-or-later

#include "texmdplugin.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>

#include <KParts/PartLoader>
#include <KParts/ReadOnlyPart>
#include <KConfig>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QAction>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <okular/interfaces/viewerinterface.h>

K_PLUGIN_FACTORY_WITH_JSON(TexMdPluginFactory, "texmdpreview.json", registerPlugin<TexMdPlugin>();)

// ---------------------------------------------------------------------------
// Web page that sends clicked links to the system browser instead of
// navigating away from the preview.
class TexMdPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (type == NavigationTypeLinkClicked && isMainFrame) {
            // in-page anchors (#section) are fine
            if (url.adjusted(QUrl::RemoveFragment) == this->url().adjusted(QUrl::RemoveFragment))
                return true;
            QDesktopServices::openUrl(url);
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
TexMdPlugin::TexMdPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
{
}

QObject *TexMdPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new TexMdView(this, mainWindow);
}

// ---------------------------------------------------------------------------
TexMdView::TexMdView(TexMdPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mw(mainWindow)
{
    m_toolview = m_mw->createToolView(plugin,
                                      QStringLiteral("texmdpreview_toolview"),
                                      KTextEditor::MainWindow::Right,
                                      QIcon::fromTheme(QStringLiteral("text-markdown")),
                                      QStringLiteral("TeX/MD Preview"));

    // Kate's tool view auto-adds child widgets to its own vbox layout.
    m_stack = new QStackedWidget(m_toolview);

    // --- info page (unsupported file) ---
    m_infoPage = new QWidget;
    auto *il = new QVBoxLayout(m_infoPage);
    m_infoLabel = new QLabel(QStringLiteral("Open a .md or .tex file to preview it."));
    m_infoLabel->setAlignment(Qt::AlignCenter);
    m_infoLabel->setWordWrap(true);
    il->addWidget(m_infoLabel);
    m_stack->addWidget(m_infoPage);

    // --- tex page ---
    m_texPage = new QWidget;
    auto *tl = new QVBoxLayout(m_texPage);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(2);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_compileBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("run-build")), QStringLiteral("Compile"));
    m_compileBtn->setToolTip(QStringLiteral("The PDF follows the cursor line.\n"
                                            "Shift+click in the PDF jumps back to the source.\n"
                                            "Ctrl+Alt+J re-centres the PDF on the cursor line."));
    bar->addWidget(m_status, 1);
    bar->addWidget(m_compileBtn);
    tl->addLayout(bar);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->hide();
    tl->addWidget(m_log);
    m_pdfHolder = new QWidget;
    auto *pl = new QVBoxLayout(m_pdfHolder);
    pl->setContentsMargins(0, 0, 0, 0);
    m_pdfPlaceholder = new QLabel(QStringLiteral("No PDF yet. Save the file (Ctrl+S) to compile."));
    m_pdfPlaceholder->setAlignment(Qt::AlignCenter);
    m_pdfPlaceholder->setWordWrap(true);
    pl->addWidget(m_pdfPlaceholder);
    tl->addWidget(m_pdfHolder, 1);
    m_stack->addWidget(m_texPage);
    connect(m_compileBtn, &QPushButton::clicked, this, [this] {
        if (m_doc && m_doc->isModified())
            m_doc->documentSave(); // save triggers compile
        else
            compile();
    });

    // cursor-follow: sync shortly after the cursor settles on a new line
    m_followTimer.setSingleShot(true);
    m_followTimer.setInterval(250);
    connect(&m_followTimer, &QTimer::timeout, this, [this] {
        if (m_view && m_view->cursorPosition().line() != m_lastSyncedLine)
            forwardSearch();
    });

    // forward search shortcut, active anywhere in this Kate window
    m_syncAction = new QAction(QStringLiteral("Sync Preview to Editor"), this);
    m_syncAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
    m_syncAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_syncAction, &QAction::triggered, this, [this] {
        if (m_kind == Kind::Tex) {
            m_lastSyncedLine = -1;
            forwardSearch();
        } else if (m_kind == Kind::Markdown) {
            syncMarkdownScroll();
        }
    });
    m_mw->window()->addAction(m_syncAction);

    // --- latex process ---
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { onCompileFinished(code); });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_status->setText(QStringLiteral("<b style='color:#d33'>latexmk not found.</b> Install it (Fedora: texlive-latexmk)."));
            m_recompileQueued = false;
        }
    });

    // --- markdown debounce ---
    m_mdTimer.setSingleShot(true);
    m_mdTimer.setInterval(200);
    connect(&m_mdTimer, &QTimer::timeout, this, &TexMdView::pushMarkdown);

    connect(m_mw, &KTextEditor::MainWindow::viewChanged, this, &TexMdView::onViewChanged);
    onViewChanged(m_mw->activeView());
}

TexMdView::~TexMdView()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    m_viewer = nullptr;
    delete m_part.data();
    delete m_toolview;
}

TexMdView::Kind TexMdView::kindOf(KTextEditor::Document *doc) const
{
    if (!doc)
        return Kind::None;
    const QString suffix = QFileInfo(doc->url().path()).suffix().toLower();
    static const QStringList md = {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"),
                                   QStringLiteral("mkd"), QStringLiteral("qmd"), QStringLiteral("rmd")};
    if (md.contains(suffix))
        return Kind::Markdown;
    if (suffix == QLatin1String("tex"))
        return Kind::Tex;
    // unsaved documents: fall back to the highlighting mode
    const QString mode = doc->mode();
    if (mode == QLatin1String("Markdown"))
        return Kind::Markdown;
    if (mode == QLatin1String("LaTeX"))
        return Kind::Tex;
    return Kind::None;
}

void TexMdView::onViewChanged(KTextEditor::View *view)
{
    for (const auto &c : std::as_const(m_docConns))
        disconnect(c);
    m_docConns.clear();

    m_view = view;
    m_doc = view ? view->document() : nullptr;
    m_kind = kindOf(m_doc);
    m_lastSyncedLine = -1;

    if (m_doc) {
        auto refresh = [this] { QTimer::singleShot(0, this, [this] { onViewChanged(m_mw->activeView()); }); };
        m_docConns << connect(m_doc, &KTextEditor::Document::textChanged, this, [this] {
            if (m_kind == Kind::Markdown)
                m_mdTimer.start();
        });
        m_docConns << connect(m_doc, &KTextEditor::Document::documentSavedOrUploaded, this, [this] {
            if (m_kind == Kind::Tex)
                compile();
        });
        m_docConns << connect(m_doc, &KTextEditor::Document::documentUrlChanged, this, refresh);
        m_docConns << connect(m_doc, &KTextEditor::Document::modeChanged, this, refresh);
        m_docConns << connect(view, &KTextEditor::View::cursorPositionChanged, this, [this] {
            if (m_kind == Kind::Tex)
                m_followTimer.start();
            else if (m_kind == Kind::Markdown && mdCursorLine() != m_lastSyncedLine)
                syncMarkdownScroll();
        });
    }

    switch (m_kind) {
    case Kind::Markdown:
        if (!m_web) {
            m_web = new QWebEngineView;
            auto *page = new TexMdPage(m_web);
            page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
            page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
            m_web->setPage(page);
            m_web->setContextMenuPolicy(Qt::NoContextMenu);
            connect(m_web, &QWebEngineView::loadFinished, this, &TexMdView::onWebLoaded);
            m_stack->addWidget(m_web);
            const QString index = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                         QStringLiteral("texmdpreview/index.html"));
            if (index.isEmpty())
                m_web->setHtml(QStringLiteral("<p>texmdpreview/index.html not found in the data dirs. "
                                              "Did <code>cmake --install</code> run?</p>"));
            else
                m_web->load(QUrl::fromLocalFile(index));
        }
        m_stack->setCurrentWidget(m_web);
        pushMarkdown();
        break;

    case Kind::Tex: {
        m_stack->setCurrentWidget(m_texPage);
        if (m_proc->state() != QProcess::NotRunning)
            break; // status already says "Compiling"
        m_log->hide();
        const QString root = rootTexFile();
        if (root.isEmpty()) {
            m_status->setText(QStringLiteral("Save the file to compile."));
            break;
        }
        const QFileInfo fi(root);
        const QString pdf = pdfForRoot(root);
        m_status->setText(QStringLiteral("Root: <b>%1</b> — saving recompiles.").arg(fi.fileName().toHtmlEscaped()));
        if (QFileInfo::exists(pdf))
            showPdf(pdf);
        break;
    }

    case Kind::None:
        m_stack->setCurrentWidget(m_infoPage);
        break;
    }
}

// ---------------------------------------------------------------------------
// Markdown

void TexMdView::onWebLoaded(bool ok)
{
    if (!ok)
        return;
    m_webReady = true;

    // theme from the Plasma palette
    const QPalette pal = m_toolview->palette();
    const QJsonArray theme{pal.color(QPalette::Base).name(), pal.color(QPalette::Text).name(),
                           pal.color(QPalette::Link).name(), pal.color(QPalette::Mid).name(),
                           pal.color(QPalette::AlternateBase).name()};
    m_web->page()->runJavaScript(QStringLiteral("texmd.setTheme.apply(null,%1);")
                                     .arg(QString::fromUtf8(QJsonDocument(theme).toJson(QJsonDocument::Compact))));

    // optional shared macros: ~/.config/texmdpreview/macros.tex
    QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/texmdpreview/macros.tex"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray a{QString::fromUtf8(f.readAll())};
        m_web->page()->runJavaScript(QStringLiteral("texmd.setMacros.apply(null,%1);")
                                         .arg(QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact))));
    }
    pushMarkdown();
}

void TexMdView::pushMarkdown()
{
    if (!m_web || !m_webReady || !m_doc || m_kind != Kind::Markdown)
        return;
    QString base;
    if (m_doc->url().isLocalFile())
        base = QUrl::fromLocalFile(QFileInfo(m_doc->url().toLocalFile()).absolutePath() + QLatin1Char('/')).toString();
    m_lastSyncedLine = mdCursorLine();
    const QJsonArray args{m_doc->text(), base, m_lastSyncedLine};
    m_web->page()->runJavaScript(QStringLiteral("texmd.render.apply(null,%1);")
                                     .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))));
}

int TexMdView::mdCursorLine() const
{
    return m_view ? m_view->cursorPosition().line() : 0;
}

void TexMdView::syncMarkdownScroll()
{
    if (!m_web || !m_webReady || m_kind != Kind::Markdown)
        return;
    m_lastSyncedLine = mdCursorLine();
    m_web->page()->runJavaScript(QStringLiteral("texmd.scrollToLine(%1);").arg(m_lastSyncedLine));
}

// ---------------------------------------------------------------------------
// LaTeX

QString TexMdView::magicComment(const QString &key) const
{
    if (!m_doc)
        return {};
    const QRegularExpression re(QStringLiteral("^\\s*%\\s*!\\s*TeX\\s+%1\\s*=\\s*(.+?)\\s*$").arg(key),
                                QRegularExpression::CaseInsensitiveOption);
    const int n = std::min(m_doc->lines(), 30);
    for (int i = 0; i < n; ++i) {
        const auto m = re.match(m_doc->line(i));
        if (m.hasMatch())
            return m.captured(1);
    }
    return {};
}

QString TexMdView::rootTexFile() const
{
    if (!m_doc || !m_doc->url().isLocalFile())
        return {};
    const QFileInfo self(m_doc->url().toLocalFile());
    const QString root = magicComment(QStringLiteral("root")); // % !TEX root = main.tex
    if (!root.isEmpty())
        return QFileInfo(self.dir(), root).absoluteFilePath();
    return self.absoluteFilePath();
}

void TexMdView::compile()
{
    if (m_kind != Kind::Tex)
        return;
    if (m_proc->state() != QProcess::NotRunning) {
        m_recompileQueued = true;
        return;
    }
    const QString root = rootTexFile();
    if (root.isEmpty()) {
        m_status->setText(QStringLiteral("Save the file to compile."));
        return;
    }
    const QFileInfo fi(root);
    if (!fi.exists()) {
        m_status->setText(QStringLiteral("<b style='color:#d33'>Root file not found:</b> %1").arg(root.toHtmlEscaped()));
        return;
    }

    // % !TEX program = xelatex | lualatex   (default pdflatex)
    const QString prog = magicComment(QStringLiteral("program")).toLower();
    QString engine = QStringLiteral("-pdf");
    if (prog.contains(QLatin1String("xelatex")))
        engine = QStringLiteral("-pdfxe");
    else if (prog.contains(QLatin1String("lualatex")))
        engine = QStringLiteral("-pdflua");

    m_runningRoot = root;
    m_recompileQueued = false;
    m_status->setText(QStringLiteral("Compiling <b>%1</b>…").arg(fi.fileName().toHtmlEscaped()));
    m_proc->setWorkingDirectory(fi.absolutePath());
    m_proc->start(QStringLiteral("latexmk"),
                  {engine, QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-synctex=1"),
                   QStringLiteral("-file-line-error"), fi.fileName()});
}

void TexMdView::onCompileFinished(int exitCode)
{
    const QString out = QString::fromLocal8Bit(m_proc->readAll());
    const QFileInfo fi(m_runningRoot);
    const QString pdf = pdfForRoot(m_runningRoot);

    if (exitCode == 0) {
        m_status->setText(QStringLiteral("<span style='color:#2a2'>✓</span> Compiled <b>%1</b>").arg(fi.fileName().toHtmlEscaped()));
        m_log->hide();
    } else {
        // pull out "file.tex:12: message" and "! message" lines
        QStringList errs;
        static const QRegularExpression fileLine(QStringLiteral("^\\S+\\.tex:\\d+: .*$"));
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (const QString &l : lines) {
            if (fileLine.match(l).hasMatch() || l.startsWith(QLatin1String("! "))) {
                if (!errs.contains(l))
                    errs << l;
            }
            if (errs.size() >= 30)
                break;
        }
        if (errs.isEmpty())
            errs = lines.mid(std::max(0, int(lines.size()) - 20));
        m_status->setText(QStringLiteral("<b style='color:#d33'>✗ Errors</b> in %1").arg(fi.fileName().toHtmlEscaped()));
        m_log->setPlainText(errs.join(QLatin1Char('\n')));
        m_log->show();
    }

    if (m_kind == Kind::Tex && QFileInfo::exists(pdf) && rootTexFile() == m_runningRoot) {
        showPdf(pdf, true); // explicit reload: new PDF + new .synctex.gz
        if (exitCode == 0) {
            m_lastSyncedLine = -1;
            forwardSearch(); // land on the cursor line in the fresh PDF
        }
    }

    if (m_recompileQueued)
        compile();
}

bool TexMdView::ensurePart()
{
    if (m_part)
        return true;

    KPluginMetaData md = KPluginMetaData::findPluginById(QStringLiteral("kf6/parts"), QStringLiteral("okularpart"));
    if (!md.isValid()) {
        const auto offers = KParts::PartLoader::partsForMimeType(QStringLiteral("application/pdf"));
        for (const auto &o : offers) {
            if (o.pluginId() != QLatin1String("katepart")) {
                md = o;
                break;
            }
        }
    }
    if (!md.isValid()) {
        m_pdfPlaceholder->setText(QStringLiteral("No PDF viewer part found. Install Okular."));
        return false;
    }

    const auto res = KPluginFactory::loadFactory(md);
    if (!res.plugin) {
        m_pdfPlaceholder->setText(QStringLiteral("Could not load PDF part: %1").arg(res.errorString));
        return false;
    }
    // Give the embedded viewer its own config file, so its mouse mode is
    // independent of the standalone Okular app (which shares okularpartrc).
    // Browse mode is required for Shift+click inverse search.
    const QString cfgPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/texmdpreview-okularpartrc");
    {
        KConfig cfg(cfgPath, KConfig::SimpleConfig);
        KConfigGroup g = cfg.group(QStringLiteral("PageView"));
        g.writeEntry("MouseMode", QStringLiteral("Browse"));
        g.writeEntry("ShowSourceLocationsGraphically", true);
        cfg.sync();
    }

    // "ViewerWidget" = embedded mode: Okular hands Shift+click source
    // references to us instead of launching its own editor command.
    m_part = res.plugin->create<KParts::ReadOnlyPart>(
        this, QVariantList{QStringLiteral("ViewerWidget"), QStringLiteral("ConfigFileName=") + cfgPath});
    if (!m_part || !m_part->widget()) {
        delete m_part.data();
        m_pdfPlaceholder->setText(QStringLiteral("PDF part loaded but gave no widget."));
        return false;
    }
    m_pdfHolder->layout()->addWidget(m_part->widget());
    m_pdfPlaceholder->hide();

    m_viewer = qobject_cast<Okular::ViewerInterface *>(m_part.data());
    if (!m_viewer)
        qWarning() << "texmdpreview: PDF part is not Okular's (" << md.pluginId() << "), SyncTeX disabled";
    if (m_viewer) {
        m_viewer->setWatchFileModeEnabled(false); // we reload after each compile
        m_viewer->setShowSourceLocationsGraphically(true);
        // string-based connect: the signal lives on the part, see viewerinterface.h
        connect(m_part.data(), SIGNAL(openSourceReference(QString, int, int)), this, SLOT(onSourceReference(QString, int, int)));
    }
    return true;
}

void TexMdView::showPdf(const QString &pdfPath, bool forceReload)
{
    if (!ensurePart())
        return;
    const QUrl url = QUrl::fromLocalFile(pdfPath);
    if (forceReload || m_part->url() != url)
        m_part->openUrl(url);
}

QString TexMdView::pdfForRoot(const QString &root) const
{
    const QFileInfo fi(root);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".pdf");
}

// ---------------------------------------------------------------------------
// SyncTeX

void TexMdView::forwardSearch()
{
    if (!m_viewer || !m_part || !m_doc || m_kind != Kind::Tex || !m_doc->url().isLocalFile())
        return;
    KTextEditor::View *view = m_mw->activeView();
    if (!view || view->document() != m_doc)
        return;
    // only if the PDF on screen belongs to this document's root
    const QString pdf = pdfForRoot(rootTexFile());
    if (m_part->url() != QUrl::fromLocalFile(pdf)) {
        if (!QFileInfo::exists(pdf))
            return;
        showPdf(pdf, true);
    }
    const KTextEditor::Cursor c = view->cursorPosition();
    m_lastSyncedLine = c.line();
    m_viewer->showSourceLocation(m_doc->url().toLocalFile(), c.line(), c.column(), true); // 0-based
}

void TexMdView::onSourceReference(const QString &absFileName, int line, int column)
{
    // SyncTeX lines are 1-based, KTextEditor cursors 0-based
    KTextEditor::View *v = m_mw->openUrl(QUrl::fromLocalFile(absFileName));
    if (!v)
        return;
    m_mw->activateView(v->document());
    // remember the line so cursor-follow doesn't bounce the PDF back
    m_lastSyncedLine = std::max(0, line - 1);
    v->setCursorPosition(KTextEditor::Cursor(m_lastSyncedLine, std::max(0, column)));
    v->setFocus();
}

#include "texmdplugin.moc"
