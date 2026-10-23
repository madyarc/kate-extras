#pragma once

#include <KTextEditor/Plugin>
#include <KTextEditor/SessionConfigInterface>
#include <KXMLGUIClient>

#include <QPointer>
#include <QSet>
#include <QVariantList>

namespace KTextEditor
{
class Document;
class MainWindow;
class View;
}

class NotebookWidget;

class KateNotebookPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateNotebookPlugin(QObject *parent, const QVariantList & = QVariantList());

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    // Files the user asked to see as JSON; those open in a normal editor tab
    void showAsJson(const QString &path);
    bool isShownAsJson(const QString &path) const;

    // The open notebook for a file, in any Kate window
    NotebookWidget *notebookFor(const QString &path) const;
    void registerNotebook(NotebookWidget *notebook);

    // Asks about every notebook with unsaved changes; false if the user cancelled
    bool queryClose(const QList<NotebookWidget *> &notebooks);
    QList<NotebookWidget *> notebooks() const;

private:
    QSet<QString> m_jsonPaths;
    QList<QPointer<NotebookWidget>> m_notebooks;
};

class KateNotebookPluginView : public QObject, public KXMLGUIClient, public KTextEditor::SessionConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(KTextEditor::SessionConfigInterface)

public:
    KateNotebookPluginView(KateNotebookPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~KateNotebookPluginView() override;

    void readSessionConfig(const KConfigGroup &config) override;
    void writeSessionConfig(KConfigGroup &config) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onViewCreated(KTextEditor::View *view);
    void openNotebook(const QString &path);
    void openActiveDocumentAsNotebook();
    void showJson(const QString &path);
    QList<NotebookWidget *> windowNotebooks() const;

    KateNotebookPlugin *m_plugin;
    KTextEditor::MainWindow *m_mainWindow;
};
