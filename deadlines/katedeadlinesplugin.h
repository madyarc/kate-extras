#pragma once

#include <KTextEditor/Plugin>
#include <KXMLGUIClient>

#include <QPointer>
#include <QVariantList>

#include <memory>

namespace KTextEditor
{
class MainWindow;
}

class DeadlineStore;
class DeadlineWidget;

class KateDeadlinesPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateDeadlinesPlugin(QObject *parent, const QVariantList & = QVariantList());

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

private:
    // Shared by every Kate window so they all show the same deadlines
    DeadlineStore *m_store;
};

class KateDeadlinesPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    KateDeadlinesPluginView(KateDeadlinesPlugin *plugin, DeadlineStore *store, KTextEditor::MainWindow *mainWindow);
    ~KateDeadlinesPluginView() override;

private:
    KTextEditor::MainWindow *m_mainWindow;
    std::unique_ptr<QWidget> m_toolView;
    QPointer<DeadlineWidget> m_widget;
};
