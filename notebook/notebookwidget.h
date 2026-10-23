#pragma once

#include "cellwidget.h"
#include "kernel.h"
#include "notebook.h"

#include <QDateTime>
#include <QWidget>

class KMessageWidget;
class QAction;
class QComboBox;
class QFileSystemWatcher;
class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

// A notebook in a Kate tab. Kate shows windowTitle() on the tab and calls shouldClose()
// before closing it.
class NotebookWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NotebookWidget(const QString &path, QWidget *parent = nullptr);

    QString path() const;
    bool isModified() const;
    bool save();

    // Asks about unsaved changes; false means the user cancelled
    Q_INVOKABLE bool shouldClose();
    // Forget unsaved changes, e.g. after the user chose "Discard" when quitting
    void discardChanges();
    // For when closing cannot be cancelled: save or discard unsaved changes
    void saveOrDiscard();

Q_SIGNALS:
    void showJsonRequested(const QString &path);

private:
    void buildToolBar(QVBoxLayout *layout);
    bool load(QString *error);
    void reload();
    void setModified(bool modified);
    void showMessage(const QString &text, int type, const QList<QAction *> &actions = {});

    CellWidget *insertCell(int index, const NotebookCell &cell);
    int indexOf(CellWidget *cell) const;
    void select(CellWidget *cell);
    void addCell(const QString &type);
    void deleteCell();
    void moveCell(int delta);
    void clearAllOutputs();

    void run(CellWidget *cell, CellWidget::RunMode mode);
    void runAll();
    void enqueue(CellWidget *cell);
    void pump();
    void cancelQueue();
    void interruptKernel();
    void restartKernel();
    void choosePython();
    QString python();
    void updateKernelStatus();

    void onFileChanged();

    QString m_path;
    Notebook m_notebook;
    QList<CellWidget *> m_cells;
    CellWidget *m_current = nullptr;
    bool m_modified = false;

    Kernel *m_kernel;
    QStringList m_queue; // cell tokens waiting to run
    QString m_running;
    QString m_pythonOverride;

    QFileSystemWatcher *m_watcher;
    QDateTime m_savedModified;
    qint64 m_savedSize = -1;

    KMessageWidget *m_message;
    QScrollArea *m_scrollArea;
    QWidget *m_cellContainer;
    QVBoxLayout *m_cellLayout;
    QComboBox *m_typeCombo;
    QLabel *m_kernelLabel;
    QToolButton *m_pythonButton;
};
