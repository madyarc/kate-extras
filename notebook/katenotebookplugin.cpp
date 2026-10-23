#include "katenotebookplugin.h"
#include "notebookwidget.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KTextEditor/Application>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <KXMLGUIFactory>
#include <KXmlGuiWindow>

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>

K_PLUGIN_FACTORY_WITH_JSON(KateNotebookPluginFactory, "katenotebookplugin.json", registerPlugin<KateNotebookPlugin>();)

namespace
{
QString notebookPath(const KTextEditor::Document *document)
{
    const QUrl url = document->url();
    if (!url.isLocalFile() || !url.path().endsWith(QLatin1String(".ipynb"), Qt::CaseInsensitive)) {
        return {};
    }
    return QFileInfo(url.toLocalFile()).absoluteFilePath();
}
}

KateNotebookPlugin::KateNotebookPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
{
    // Once its JSON tab is closed, a notebook opens as a notebook again
    connect(KTextEditor::Editor::instance()->application(), &KTextEditor::Application::documentWillBeDeleted, this, [this](KTextEditor::Document *document) {
        m_jsonPaths.remove(notebookPath(document));
    });
}

QObject *KateNotebookPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new KateNotebookPluginView(this, mainWindow);
}

void KateNotebookPlugin::showAsJson(const QString &path)
{
    m_jsonPaths.insert(path);
}

bool KateNotebookPlugin::isShownAsJson(const QString &path) const
{
    return m_jsonPaths.contains(path);
}

NotebookWidget *KateNotebookPlugin::notebookFor(const QString &path) const
{
    for (const auto &notebook : m_notebooks) {
        if (notebook && notebook->path() == path) {
            return notebook;
        }
    }
    return nullptr;
}

void KateNotebookPlugin::registerNotebook(NotebookWidget *notebook)
{
    m_notebooks.removeAll(nullptr);
    m_notebooks.append(notebook);
}

QList<NotebookWidget *> KateNotebookPlugin::notebooks() const
{
    QList<NotebookWidget *> result;
    for (const auto &notebook : m_notebooks) {
        if (notebook) {
            result.append(notebook);
        }
    }
    return result;
}

bool KateNotebookPlugin::queryClose(const QList<NotebookWidget *> &notebooks)
{
    for (NotebookWidget *notebook : notebooks) {
        if (notebook->isModified() && !notebook->shouldClose()) {
            return false;
        }
    }
    return true;
}

KateNotebookPluginView::KateNotebookPluginView(KateNotebookPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_plugin(plugin)
    , m_mainWindow(mainWindow)
{
    KXMLGUIClient::setComponentName(QStringLiteral("katenotebook"), i18n("Notebook"));

    QAction *openAsNotebook = actionCollection()->addAction(QStringLiteral("notebook_open"));
    openAsNotebook->setText(i18n("Open as Notebook"));
    openAsNotebook->setIcon(QIcon::fromTheme(QStringLiteral("application-x-ipynb+json"), QIcon::fromTheme(QStringLiteral("document-open"))));
    connect(openAsNotebook, &QAction::triggered, this, &KateNotebookPluginView::openActiveDocumentAsNotebook);

    setXML(QStringLiteral(R"(<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="katenotebook" version="1">
  <MenuBar>
    <Menu name="tools">
      <Action name="notebook_open"/>
    </Menu>
  </MenuBar>
</gui>)"));
    m_mainWindow->guiFactory()->addClient(this);

    connect(m_mainWindow, &KTextEditor::MainWindow::viewCreated, this, &KateNotebookPluginView::onViewCreated);

    // Closing the window: ask about unsaved notebooks first, and keep it open on Cancel
    m_mainWindow->window()->installEventFilter(this);

    // Kate's Quit (Cmd+Q) does not close windows; it checks documents and exits, so
    // notebooks would be lost silently. Ask before Kate's own (queued) quit handler runs.
    if (auto *window = qobject_cast<KXmlGuiWindow *>(m_mainWindow->window())) {
        if (QAction *quit = window->actionCollection()->action(QStringLiteral("file_quit"))) {
            connect(quit, &QAction::triggered, this, [this, window]() {
                if (!m_plugin->queryClose(m_plugin->notebooks())) {
                    // Kate posted its quit call to the window just before this ran
                    QCoreApplication::removePostedEvents(window, QEvent::MetaCall);
                }
            });
        }
    }
}

KateNotebookPluginView::~KateNotebookPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);
    for (NotebookWidget *notebook : windowNotebooks()) {
        // The plugin is going away (disabled, or Kate is quitting), so a notebook cannot stay open
        notebook->saveOrDiscard();
        QPointer<NotebookWidget> guard(notebook);
        m_mainWindow->removeWidget(notebook);
        // removeWidget() only schedules the deletion, which could run after this plugin is unloaded
        delete guard;
    }
}

void KateNotebookPluginView::readSessionConfig(const KConfigGroup &config)
{
    const QStringList paths = config.readEntry("Notebooks", QStringList());
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            openNotebook(path);
        }
    }
}

void KateNotebookPluginView::writeSessionConfig(KConfigGroup &config)
{
    QStringList paths;
    for (NotebookWidget *notebook : windowNotebooks()) {
        paths.append(notebook->path());
    }
    config.writeEntry("Notebooks", paths);
}

bool KateNotebookPluginView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mainWindow->window() && event->type() == QEvent::Close && !m_plugin->queryClose(windowNotebooks())) {
        event->ignore();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void KateNotebookPluginView::onViewCreated(KTextEditor::View *view)
{
    const QString path = notebookPath(view->document());
    if (path.isEmpty() || m_plugin->isShownAsJson(path)) {
        return;
    }
    // Let Kate finish opening the file before replacing its tab
    QPointer<KTextEditor::Document> document = view->document();
    QTimer::singleShot(0, this, [this, document, path]() {
        if (!document || document->isModified()) {
            return;
        }
        openNotebook(path);
        KTextEditor::Editor::instance()->application()->closeDocument(document);
    });
}

void KateNotebookPluginView::openNotebook(const QString &path)
{
    if (NotebookWidget *existing = m_plugin->notebookFor(path)) {
        // Already open, possibly in another window
        for (KTextEditor::MainWindow *window : KTextEditor::Editor::instance()->application()->mainWindows()) {
            if (window->widgets().contains(existing)) {
                window->activateWidget(existing);
                window->window()->raise();
                window->window()->activateWindow();
                return;
            }
        }
    }

    auto *notebook = new NotebookWidget(path);
    connect(notebook, &NotebookWidget::showJsonRequested, this, &KateNotebookPluginView::showJson);
    m_plugin->registerNotebook(notebook);
    if (!m_mainWindow->addWidget(notebook)) {
        delete notebook;
    }
}

void KateNotebookPluginView::openActiveDocumentAsNotebook()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    const QString path = view ? notebookPath(view->document()) : QString();
    if (path.isEmpty()) {
        QMessageBox::information(m_mainWindow->window(), i18n("Open as Notebook"), i18n("The current file is not a Jupyter notebook (.ipynb)."));
        return;
    }
    KTextEditor::Document *document = view->document();
    if (document->isModified() && !document->documentSave()) {
        return;
    }
    openNotebook(path);
    KTextEditor::Editor::instance()->application()->closeDocument(document);
}

void KateNotebookPluginView::showJson(const QString &path)
{
    NotebookWidget *notebook = m_plugin->notebookFor(path);
    if (notebook && notebook->isModified()) {
        const auto answer = QMessageBox::question(m_mainWindow->window(),
                                                  i18n("Show JSON"),
                                                  i18n("The JSON tab shows the file as saved. Save your changes first?"),
                                                  QMessageBox::Save | QMessageBox::Ignore | QMessageBox::Cancel,
                                                  QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !notebook->save())) {
            return;
        }
    }
    m_plugin->showAsJson(path);
    m_mainWindow->openUrl(QUrl::fromLocalFile(path));
}

QList<NotebookWidget *> KateNotebookPluginView::windowNotebooks() const
{
    QList<NotebookWidget *> result;
    const auto widgets = m_mainWindow->widgets();
    for (QWidget *widget : widgets) {
        if (auto *notebook = qobject_cast<NotebookWidget *>(widget)) {
            result.append(notebook);
        }
    }
    return result;
}

#include "katenotebookplugin.moc"
