#include "notebookwidget.h"
#include "outputview.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KSharedConfig>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
KConfigGroup pythonConfig()
{
    return KSharedConfig::openConfig(QStringLiteral("katenotebookrc"))->group(QStringLiteral("Python"));
}

// "venv" for ~/project/venv/bin/python, otherwise the path with ~ for home
QString shortPythonName(const QString &python)
{
    if (python.isEmpty()) {
        return i18n("No Python");
    }
    QDir bin = QFileInfo(python).absoluteDir();
    if (bin.dirName() == QLatin1String("bin") && QFileInfo::exists(bin.filePath(QStringLiteral("../pyvenv.cfg")))) {
        bin.cdUp();
        return bin.dirName();
    }
    QString path = python;
    if (path.startsWith(QDir::homePath())) {
        path.replace(0, QDir::homePath().size(), QStringLiteral("~"));
    }
    return path;
}
}

NotebookWidget::NotebookWidget(const QString &path, QWidget *parent)
    : QWidget(parent)
    , m_path(QFileInfo(path).absoluteFilePath())
    , m_kernel(new Kernel(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_message(new KMessageWidget(this))
    , m_scrollArea(new QScrollArea(this))
    , m_cellContainer(new QWidget)
    , m_cellLayout(new QVBoxLayout(m_cellContainer))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    buildToolBar(layout);

    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(true);
    m_message->hide();
    layout->addWidget(m_message);

    m_cellLayout->setContentsMargins(8, 8, 8, 8);
    m_cellLayout->setSpacing(6);
    auto *addRow = new QHBoxLayout;
    const std::pair<QString, QString> cellTypes[] = {{QStringLiteral("code"), i18n("Code")}, {QStringLiteral("markdown"), i18n("Markdown")}};
    for (const auto &cellType : cellTypes) {
        const QString type = cellType.first;
        auto *button = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), cellType.second, m_cellContainer);
        button->setFlat(true);
        button->setToolTip(i18n("Add a cell at the end"));
        connect(button, &QPushButton::clicked, this, [this, type]() {
            CellWidget *cell = insertCell(m_cells.size(), m_notebook.newCell(type));
            select(cell);
            cell->focusEditor();
            setModified(true);
        });
        addRow->addWidget(button);
    }
    addRow->addStretch();
    m_cellLayout->addLayout(addRow);
    m_cellLayout->addStretch(1);

    m_scrollArea->setWidget(m_cellContainer);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_scrollArea, 1);

    connect(m_kernel, &Kernel::output, this, [this](const QString &token, const QJsonObject &output) {
        for (CellWidget *cell : std::as_const(m_cells)) {
            if (cell->token() == token) {
                cell->outputView()->append(output);
                setModified(true);
            }
        }
    });
    connect(m_kernel, &Kernel::clearOutput, this, [this](const QString &token, bool wait) {
        for (CellWidget *cell : std::as_const(m_cells)) {
            if (cell->token() == token) {
                wait ? cell->outputView()->clearOnNextOutput() : cell->outputView()->clear();
            }
        }
    });
    connect(m_kernel, &Kernel::finished, this, [this](const QString &token, const QJsonValue &count, const QString &status) {
        if (token == m_running) {
            m_running.clear();
        }
        for (CellWidget *cell : std::as_const(m_cells)) {
            if (cell->token() == token) {
                cell->setExecutionCount(status == QLatin1String("aborted") ? QJsonValue(QJsonValue::Null) : count);
            }
        }
        // Like Jupyter, an error stops the rest of a "Run All"
        if (status != QLatin1String("ok")) {
            cancelQueue();
        }
        updateKernelStatus();
        pump();
    });
    connect(m_kernel, &Kernel::stateChanged, this, [this](Kernel::State state) {
        if (state == Kernel::State::Ready) {
            pump();
        } else if (state == Kernel::State::Failed) {
            cancelQueue();
            for (CellWidget *cell : std::as_const(m_cells)) {
                if (cell->token() == m_running) {
                    cell->cancelQueued();
                }
            }
            m_running.clear();
        }
        updateKernelStatus();
    });
    connect(m_kernel, &Kernel::problem, this, [this](const QString &message, bool missingJupyter) {
        QList<QAction *> actions;
        if (missingJupyter) {
            auto *choose = new QAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Choose Python…"), m_message);
            connect(choose, &QAction::triggered, this, &NotebookWidget::choosePython);
            actions << choose;
        }
        showMessage(message, missingJupyter ? KMessageWidget::Error : KMessageWidget::Warning, actions);
    });
    connect(m_kernel, &Kernel::died, this, [this]() {
        auto *restart = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Restart Kernel"), m_message);
        connect(restart, &QAction::triggered, this, [this]() {
            m_message->animatedHide();
            m_kernel->restart();
        });
        showMessage(i18n("The kernel died. Variables are lost; restart it to keep working."), KMessageWidget::Error, {restart});
    });

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        // Editors and git often replace a file in several steps; wait for them to finish
        QTimer::singleShot(300, this, &NotebookWidget::onFileChanged);
    });

    QString error;
    if (!load(&error)) {
        // load() only names the tab on success
        setModified(false);
        auto *showJson = new QAction(QIcon::fromTheme(QStringLiteral("text-x-script")), i18n("Show JSON"), m_message);
        connect(showJson, &QAction::triggered, this, [this]() {
            Q_EMIT showJsonRequested(m_path);
        });
        showMessage(error, KMessageWidget::Error, {showJson});
    }
    m_watcher->addPath(m_path);
    updateKernelStatus();
}

void NotebookWidget::buildToolBar(QVBoxLayout *layout)
{
    auto *bar = new QToolBar(this);
    bar->setIconSize(QSize(16, 16));

    // Shortcuts apply anywhere in the notebook, not only over the tool bar
    // Only the most used actions show their name; the rest are icons with tool tips
    auto addAction = [this, bar](const QString &icon, const QString &text, const QString &toolTip, auto slot, const QKeySequence &shortcut = {}) {
        QAction *action = bar->addAction(QIcon::fromTheme(icon), text);
        action->setToolTip(toolTip);
        qobject_cast<QToolButton *>(bar->widgetForAction(action))->setToolButtonStyle(Qt::ToolButtonIconOnly);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            this->addAction(action);
        }
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    QAction *runAction = addAction(QStringLiteral("media-playback-start"),
              i18n("Run"),
              i18n("Run the selected cell and move to the next one (Shift+Enter)"),
              [this]() {
                  run(m_current, CellWidget::RunAndAdvance);
              },
              QKeySequence(Qt::SHIFT | Qt::Key_Return));
    QAction *runAllAction = addAction(QStringLiteral("media-seek-forward"), i18n("Run All"), i18n("Run every cell from the top"), &NotebookWidget::runAll);
    for (QAction *action : {runAction, runAllAction}) {
        qobject_cast<QToolButton *>(bar->widgetForAction(action))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    addAction(QStringLiteral("media-playback-stop"), i18n("Interrupt"), i18n("Interrupt the running cell"), &NotebookWidget::interruptKernel);
    addAction(QStringLiteral("view-refresh"), i18n("Restart"), i18n("Restart the kernel, clearing all variables"), &NotebookWidget::restartKernel);
    addAction(QStringLiteral("edit-clear-all"), i18n("Clear Outputs"), i18n("Remove the outputs of every cell"), &NotebookWidget::clearAllOutputs);
    bar->addSeparator();

    // "+ Code" and "+ Markdown" read better with their names
    for (QAction *action : {addAction(QStringLiteral("list-add"),
                                      i18n("Code"),
                                      i18n("Insert a code cell below"),
                                      [this]() {
                                          addCell(QStringLiteral("code"));
                                      }),
                            addAction(QStringLiteral("list-add"), i18n("Markdown"), i18n("Insert a Markdown cell below"), [this]() {
                                addCell(QStringLiteral("markdown"));
                            })}) {
        qobject_cast<QToolButton *>(bar->widgetForAction(action))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    addAction(QStringLiteral("go-up"), i18n("Move Up"), i18n("Move the selected cell up"), [this]() {
        moveCell(-1);
    });
    addAction(QStringLiteral("go-down"), i18n("Move Down"), i18n("Move the selected cell down"), [this]() {
        moveCell(1);
    });
    addAction(QStringLiteral("edit-delete"), i18n("Delete"), i18n("Delete the selected cell"), &NotebookWidget::deleteCell);

    m_typeCombo = new QComboBox(bar);
    m_typeCombo->addItem(i18n("Code"), QStringLiteral("code"));
    m_typeCombo->addItem(i18n("Markdown"), QStringLiteral("markdown"));
    m_typeCombo->addItem(i18n("Raw"), QStringLiteral("raw"));
    m_typeCombo->setToolTip(i18n("Type of the selected cell"));
    connect(m_typeCombo, &QComboBox::activated, this, [this]() {
        if (m_current) {
            m_current->setType(m_typeCombo->currentData().toString());
            m_current->focusEditor();
        }
    });
    bar->addWidget(m_typeCombo);

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    m_kernelLabel = new QLabel(bar);
    m_kernelLabel->setEnabled(false);
    bar->addWidget(m_kernelLabel);

    m_pythonButton = new QToolButton(bar);
    m_pythonButton->setIcon(QIcon::fromTheme(QStringLiteral("application-x-python-bytecode"), QIcon::fromTheme(QStringLiteral("text-x-python"))));
    m_pythonButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pythonButton->setAutoRaise(true);
    connect(m_pythonButton, &QToolButton::clicked, this, &NotebookWidget::choosePython);
    bar->addWidget(m_pythonButton);
    bar->addSeparator();

    addAction(QStringLiteral("text-x-script"), i18n("JSON"), i18n("Open the notebook file as JSON in a normal editor tab"), [this]() {
        Q_EMIT showJsonRequested(m_path);
    });
    addAction(QStringLiteral("document-save"), i18n("Save"), i18n("Save the notebook"), &NotebookWidget::save, QKeySequence::Save);

    layout->addWidget(bar);
}

QString NotebookWidget::path() const
{
    return m_path;
}

bool NotebookWidget::isModified() const
{
    return m_modified;
}

bool NotebookWidget::load(QString *error)
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = i18n("Could not open %1: %2", m_path, file.errorString());
        return false;
    }
    Notebook notebook;
    if (!notebook.load(file.readAll(), error)) {
        return false;
    }
    m_notebook = notebook;

    cancelQueue();
    qDeleteAll(m_cells);
    m_cells.clear();
    m_current = nullptr;
    for (int i = 0; i < m_notebook.cells.size(); ++i) {
        insertCell(i, m_notebook.cells.at(i));
    }
    if (m_cells.isEmpty()) {
        insertCell(0, m_notebook.newCell(QStringLiteral("code")));
    }
    select(m_cells.first());

    const QFileInfo info(m_path);
    m_savedModified = info.lastModified();
    m_savedSize = info.size();
    setModified(false);
    return true;
}

void NotebookWidget::reload()
{
    m_message->animatedHide();
    QString error;
    if (!load(&error)) {
        showMessage(error, KMessageWidget::Error);
    }
}

bool NotebookWidget::save()
{
    m_notebook.cells.clear();
    for (const CellWidget *cell : std::as_const(m_cells)) {
        m_notebook.cells.append(cell->cell());
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_notebook.toJson()) < 0 || !file.commit()) {
        QMessageBox::critical(this, i18n("Save Notebook"), i18n("Could not save %1: %2", m_path, file.errorString()));
        return false;
    }

    const QFileInfo info(m_path);
    m_savedModified = info.lastModified();
    m_savedSize = info.size();
    // QSaveFile replaces the file, which drops it from the watcher
    if (!m_watcher->files().contains(m_path)) {
        m_watcher->addPath(m_path);
    }
    setModified(false);
    return true;
}

bool NotebookWidget::shouldClose()
{
    if (!m_modified) {
        return true;
    }
    const auto answer = QMessageBox::warning(this,
                                             i18n("Close Notebook"),
                                             i18n("The notebook “%1” has unsaved changes. Do you want to save them?", QFileInfo(m_path).fileName()),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                             QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return save();
    }
    if (answer == QMessageBox::Discard) {
        discardChanges();
        return true;
    }
    return false;
}

void NotebookWidget::discardChanges()
{
    setModified(false);
}

void NotebookWidget::saveOrDiscard()
{
    if (!m_modified) {
        return;
    }
    const auto answer = QMessageBox::warning(this,
                                             i18n("Close Notebook"),
                                             i18n("The notebook “%1” has unsaved changes. Do you want to save them?", QFileInfo(m_path).fileName()),
                                             QMessageBox::Save | QMessageBox::Discard,
                                             QMessageBox::Save);
    if (answer != QMessageBox::Save || !save()) {
        discardChanges();
    }
}

void NotebookWidget::setModified(bool modified)
{
    m_modified = modified;
    const QString name = QFileInfo(m_path).fileName();
    setWindowTitle(modified ? name + QStringLiteral(" *") : name);
}

void NotebookWidget::showMessage(const QString &text, int type, const QList<QAction *> &actions)
{
    const auto oldActions = m_message->actions();
    for (QAction *action : oldActions) {
        m_message->removeAction(action);
    }
    for (QAction *action : actions) {
        m_message->addAction(action);
    }
    m_message->setMessageType(KMessageWidget::MessageType(type));
    m_message->setText(text);
    m_message->animatedShow();
}

CellWidget *NotebookWidget::insertCell(int index, const NotebookCell &data)
{
    auto *cell = new CellWidget(data, m_notebook.language(), QFileInfo(m_path).absolutePath(), m_scrollArea, m_cellContainer);
    m_cells.insert(index, cell);
    m_cellLayout->insertWidget(index, cell);

    connect(cell, &CellWidget::activated, this, &NotebookWidget::select);
    connect(cell, &CellWidget::runRequested, this, &NotebookWidget::run);
    connect(cell, &CellWidget::saveRequested, this, &NotebookWidget::save);
    connect(cell, &CellWidget::changed, this, [this]() {
        setModified(true);
    });
    return cell;
}

int NotebookWidget::indexOf(CellWidget *cell) const
{
    return m_cells.indexOf(cell);
}

void NotebookWidget::select(CellWidget *cell)
{
    if (cell == m_current) {
        return;
    }
    if (m_current) {
        m_current->setSelected(false);
    }
    m_current = cell;
    if (cell) {
        cell->setSelected(true);
        m_typeCombo->setCurrentIndex(m_typeCombo->findData(cell->type()));
    }
}

void NotebookWidget::addCell(const QString &type)
{
    const int index = m_current ? indexOf(m_current) + 1 : int(m_cells.size());
    CellWidget *cell = insertCell(index, m_notebook.newCell(type));
    select(cell);
    cell->focusEditor();
    setModified(true);
}

void NotebookWidget::deleteCell()
{
    if (!m_current) {
        return;
    }
    const int index = indexOf(m_current);
    CellWidget *cell = m_cells.takeAt(index);
    m_queue.removeAll(cell->token());
    m_current = nullptr;
    cell->hide();
    cell->deleteLater();

    if (m_cells.isEmpty()) {
        insertCell(0, m_notebook.newCell(QStringLiteral("code")));
    }
    CellWidget *next = m_cells.at(qMin(index, int(m_cells.size()) - 1));
    select(next);
    next->focusEditor();
    setModified(true);
}

void NotebookWidget::moveCell(int delta)
{
    if (!m_current) {
        return;
    }
    const int from = indexOf(m_current);
    const int to = from + delta;
    if (to < 0 || to >= m_cells.size()) {
        return;
    }
    m_cells.move(from, to);
    m_cellLayout->removeWidget(m_current);
    m_cellLayout->insertWidget(to, m_current);
    m_current->focusEditor();
    setModified(true);
}

void NotebookWidget::clearAllOutputs()
{
    for (CellWidget *cell : std::as_const(m_cells)) {
        if (cell->type() == QLatin1String("code")) {
            cell->outputView()->clear();
            cell->setExecutionCount(QJsonValue::Null);
        }
    }
    setModified(true);
}

void NotebookWidget::run(CellWidget *cell, CellWidget::RunMode mode)
{
    if (!cell) {
        return;
    }
    if (cell->type() == QLatin1String("code")) {
        enqueue(cell);
    } else {
        cell->render();
    }

    const int index = indexOf(cell);
    if (mode == CellWidget::RunAndAdvance) {
        // Shift+Enter on the last cell adds a fresh one, as in Jupyter
        CellWidget *next = index + 1 < m_cells.size() ? m_cells.at(index + 1) : insertCell(index + 1, m_notebook.newCell(QStringLiteral("code")));
        select(next);
        next->focusEditor();
    } else if (mode == CellWidget::RunAndInsert) {
        CellWidget *inserted = insertCell(index + 1, m_notebook.newCell(QStringLiteral("code")));
        select(inserted);
        inserted->focusEditor();
        setModified(true);
    }
    pump();
}

void NotebookWidget::runAll()
{
    for (CellWidget *cell : std::as_const(m_cells)) {
        if (cell->type() == QLatin1String("code")) {
            enqueue(cell);
        } else {
            cell->render();
        }
    }
    pump();
}

void NotebookWidget::enqueue(CellWidget *cell)
{
    if (cell->source().trimmed().isEmpty() || m_queue.contains(cell->token()) || m_running == cell->token()) {
        return;
    }
    cell->setQueued();
    m_queue.append(cell->token());
}

void NotebookWidget::pump()
{
    if (!m_running.isEmpty() || m_queue.isEmpty()) {
        return;
    }

    switch (m_kernel->state()) {
    case Kernel::State::Ready:
        break;
    case Kernel::State::Starting:
        return; // pumped again once the kernel is ready
    case Kernel::State::Stopped:
    case Kernel::State::Failed: {
        const QString interpreter = python();
        if (interpreter.isEmpty()) {
            auto *choose = new QAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Choose Python…"), m_message);
            connect(choose, &QAction::triggered, this, &NotebookWidget::choosePython);
            showMessage(i18n("No Python interpreter was found for this notebook."), KMessageWidget::Error, {choose});
            cancelQueue();
            return;
        }
        m_message->animatedHide();
        m_kernel->start(interpreter, m_notebook.kernelName(), QFileInfo(m_path).absolutePath());
        updateKernelStatus();
        return;
    }
    }

    while (!m_queue.isEmpty()) {
        const QString token = m_queue.takeFirst();
        for (CellWidget *cell : std::as_const(m_cells)) {
            if (cell->token() == token) {
                m_running = token;
                cell->outputView()->clear();
                m_kernel->execute(token, cell->source());
                setModified(true);
                updateKernelStatus();
                return;
            }
        }
    }
}

void NotebookWidget::cancelQueue()
{
    for (CellWidget *cell : std::as_const(m_cells)) {
        if (m_queue.contains(cell->token())) {
            cell->cancelQueued();
        }
    }
    m_queue.clear();
}

void NotebookWidget::interruptKernel()
{
    cancelQueue();
    m_kernel->interrupt();
}

void NotebookWidget::restartKernel()
{
    if (m_kernel->state() != Kernel::State::Ready) {
        return;
    }
    const auto answer = QMessageBox::question(this, i18n("Restart Kernel"), i18n("Restart the kernel? All variables will be lost."));
    if (answer != QMessageBox::Yes) {
        return;
    }
    cancelQueue();
    m_kernel->restart();
}

QString NotebookWidget::python()
{
    if (!m_pythonOverride.isEmpty()) {
        return m_pythonOverride;
    }
    const QString dir = QFileInfo(m_path).absolutePath();
    const QString remembered = pythonConfig().readEntry(dir, QString());
    if (!remembered.isEmpty() && QFileInfo(remembered).isExecutable()) {
        return remembered;
    }
    return Kernel::findPython(dir);
}

void NotebookWidget::choosePython()
{
    const QString current = python();
    const QString chosen = QFileDialog::getOpenFileName(this,
                                                        i18n("Choose the Python for this notebook"),
                                                        current.isEmpty() ? QFileInfo(m_path).absolutePath() : QFileInfo(current).absolutePath());
    if (chosen.isEmpty()) {
        return;
    }
    m_pythonOverride = chosen;
    KConfigGroup config = pythonConfig();
    config.writeEntry(QFileInfo(m_path).absolutePath(), chosen);
    config.sync();

    // The next run starts a kernel with the new interpreter
    cancelQueue();
    m_running.clear();
    m_kernel->shutdown();
    m_message->animatedHide();
    updateKernelStatus();
}

void NotebookWidget::updateKernelStatus()
{
    const QString interpreter = python();
    m_pythonButton->setText(shortPythonName(interpreter));
    m_pythonButton->setToolTip(i18n("Python: %1\nClick to choose another one", interpreter.isEmpty() ? i18n("none found") : interpreter));

    switch (m_kernel->state()) {
    case Kernel::State::Stopped:
        m_kernelLabel->setText(i18n("Kernel starts on first run"));
        break;
    case Kernel::State::Starting:
        m_kernelLabel->setText(i18n("Starting kernel…"));
        break;
    case Kernel::State::Ready:
        m_kernelLabel->setText(m_running.isEmpty() ? i18n("%1 · idle", m_kernel->displayName()) : i18n("%1 · busy", m_kernel->displayName()));
        break;
    case Kernel::State::Failed:
        m_kernelLabel->setText(i18n("Kernel stopped"));
        break;
    }
}

void NotebookWidget::onFileChanged()
{
    if (!m_watcher->files().contains(m_path) && QFileInfo::exists(m_path)) {
        m_watcher->addPath(m_path);
    }
    const QFileInfo info(m_path);
    if (!info.exists() || (info.lastModified() == m_savedModified && info.size() == m_savedSize)) {
        return;
    }
    if (!m_modified) {
        reload();
        return;
    }

    auto *reloadAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reload"), m_message);
    connect(reloadAction, &QAction::triggered, this, &NotebookWidget::reload);
    auto *keep = new QAction(QIcon::fromTheme(QStringLiteral("dialog-cancel")), i18n("Keep My Changes"), m_message);
    connect(keep, &QAction::triggered, this, [this]() {
        const QFileInfo info(m_path);
        m_savedModified = info.lastModified();
        m_savedSize = info.size();
        m_message->animatedHide();
    });
    showMessage(i18n("“%1” was changed outside Kate. Reload it and lose your unsaved changes?", info.fileName()), KMessageWidget::Warning, {reloadAction, keep});
}
