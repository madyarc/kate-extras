#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTime>

class QFileSystemWatcher;

struct Deadline {
    QString id;
    QString title;
    QDate date;
    QTime time; // invalid when the deadline is for the whole day
    QString notes;
    bool done = false;

    // Whole-day deadlines are due at the end of their day
    QDateTime due() const;
    bool isOverdue() const;
};

// Owns the deadline list and keeps it in sync with a JSON file shared by all Kate windows
class DeadlineStore : public QObject
{
    Q_OBJECT

public:
    explicit DeadlineStore(QObject *parent = nullptr);

    QList<Deadline> deadlines() const;
    Deadline find(const QString &id) const;

    void add(Deadline deadline);
    void update(const Deadline &deadline);
    void remove(const QString &id);
    void setDone(const QString &id, bool done);

    QString filePath() const;

Q_SIGNALS:
    void changed();

private:
    void load();
    void save();
    void watchFile();

    QList<Deadline> m_deadlines;
    QFileSystemWatcher *m_watcher;
};
