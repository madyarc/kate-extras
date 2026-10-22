#include "deadlinestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>

QDateTime Deadline::due() const
{
    return QDateTime(date, time.isValid() ? time : QTime(23, 59, 59));
}

bool Deadline::isOverdue() const
{
    return !done && due() < QDateTime::currentDateTime();
}

DeadlineStore::DeadlineStore(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());

    // Another Kate process may edit the file; coalesce bursts of change events into one reload
    auto *reloadTimer = new QTimer(this);
    reloadTimer->setSingleShot(true);
    reloadTimer->setInterval(150);
    connect(reloadTimer, &QTimer::timeout, this, [this]() {
        load();
        watchFile();
        Q_EMIT changed();
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, reloadTimer, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, reloadTimer, qOverload<>(&QTimer::start));

    load();
    watchFile();
}

QString DeadlineStore::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/katedeadlines/deadlines.json");
}

QList<Deadline> DeadlineStore::deadlines() const
{
    return m_deadlines;
}

Deadline DeadlineStore::find(const QString &id) const
{
    for (const Deadline &deadline : m_deadlines) {
        if (deadline.id == id) {
            return deadline;
        }
    }
    return {};
}

void DeadlineStore::add(Deadline deadline)
{
    deadline.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_deadlines.append(deadline);
    save();
}

void DeadlineStore::update(const Deadline &deadline)
{
    for (Deadline &existing : m_deadlines) {
        if (existing.id == deadline.id) {
            existing = deadline;
            save();
            return;
        }
    }
}

void DeadlineStore::remove(const QString &id)
{
    m_deadlines.removeIf([&id](const Deadline &deadline) {
        return deadline.id == id;
    });
    save();
}

void DeadlineStore::setDone(const QString &id, bool done)
{
    Deadline deadline = find(id);
    if (!deadline.id.isEmpty() && deadline.done != done) {
        deadline.done = done;
        update(deadline);
    }
}

void DeadlineStore::load()
{
    m_deadlines.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("deadlines")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Deadline deadline;
        deadline.id = object.value(QStringLiteral("id")).toString();
        deadline.title = object.value(QStringLiteral("title")).toString();
        deadline.date = QDate::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
        deadline.time = QTime::fromString(object.value(QStringLiteral("time")).toString(), QStringLiteral("HH:mm"));
        deadline.notes = object.value(QStringLiteral("notes")).toString();
        deadline.done = object.value(QStringLiteral("done")).toBool();
        if (!deadline.id.isEmpty() && deadline.date.isValid()) {
            m_deadlines.append(deadline);
        }
    }

    std::sort(m_deadlines.begin(), m_deadlines.end(), [](const Deadline &a, const Deadline &b) {
        return a.due() < b.due();
    });
}

void DeadlineStore::save()
{
    std::sort(m_deadlines.begin(), m_deadlines.end(), [](const Deadline &a, const Deadline &b) {
        return a.due() < b.due();
    });

    QJsonArray array;
    for (const Deadline &deadline : std::as_const(m_deadlines)) {
        QJsonObject object{
            {QStringLiteral("id"), deadline.id},
            {QStringLiteral("title"), deadline.title},
            {QStringLiteral("date"), deadline.date.toString(Qt::ISODate)},
            {QStringLiteral("done"), deadline.done},
        };
        if (deadline.time.isValid()) {
            object.insert(QStringLiteral("time"), deadline.time.toString(QStringLiteral("HH:mm")));
        }
        if (!deadline.notes.isEmpty()) {
            object.insert(QStringLiteral("notes"), deadline.notes);
        }
        array.append(object);
    }

    QSaveFile file(filePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("deadlines"), array}}).toJson());
        file.commit();
    }

    Q_EMIT changed();
}

void DeadlineStore::watchFile()
{
    // QSaveFile replaces the file, which drops it from the watcher, so re-add it each time
    const QString path = filePath();
    const QString directory = QFileInfo(path).absolutePath();
    if (!m_watcher->directories().contains(directory)) {
        m_watcher->addPath(directory);
    }
    if (QFile::exists(path) && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
}
