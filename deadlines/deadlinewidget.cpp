#include "deadlinewidget.h"
#include "deadlinedialog.h"
#include "deadlinestore.h"

#include <KLocalizedString>

#include <QCalendarWidget>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QShortcut>
#include <QTextCharFormat>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr int s_idRole = Qt::UserRole;
constexpr int s_dateRole = Qt::UserRole + 1;
constexpr int s_upcomingDays = 14;

const QColor s_overdueColor(0xda, 0x44, 0x53);

QToolButton *makeButton(const QString &iconName, const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setIcon(QIcon::fromTheme(iconName));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    return button;
}
}

DeadlineWidget::DeadlineWidget(DeadlineStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_calendar(new QCalendarWidget(this))
    , m_filter(new QComboBox(this))
    , m_summary(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_addButton(makeButton(QStringLiteral("list-add"), i18n("Add deadline"), this))
    , m_editButton(makeButton(QStringLiteral("document-edit"), i18n("Edit deadline"), this))
    , m_deleteButton(makeButton(QStringLiteral("list-remove"), i18n("Delete deadline"), this))
{
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendar->setGridVisible(false);
    m_calendar->setToolTip(i18n("Double-click a day to add a deadline on it"));

    m_filter->addItem(i18n("Selected day"), SelectedDay);
    m_filter->addItem(i18n("Upcoming and overdue"), Upcoming);
    m_filter->addItem(i18n("All deadlines"), All);
    m_filter->setCurrentIndex(Upcoming);

    m_summary->setWordWrap(true);

    m_list->setAlternatingRowColors(true);
    m_list->setWordWrap(true);
    m_list->setToolTip(i18n("Tick a deadline to mark it done; double-click to edit"));

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(m_filter, 1);
    toolbar->addWidget(m_addButton);
    toolbar->addWidget(m_editButton);
    toolbar->addWidget(m_deleteButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_calendar);
    layout->addLayout(toolbar);
    layout->addWidget(m_summary);
    layout->addWidget(m_list, 1);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, [this]() {
        if (m_filter->currentData().toInt() == SelectedDay) {
            populateList();
        }
    });
    connect(m_calendar, &QCalendarWidget::activated, this, &DeadlineWidget::addDeadlineOn);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &DeadlineWidget::populateList);
    connect(m_addButton, &QToolButton::clicked, this, &DeadlineWidget::addDeadline);
    connect(m_editButton, &QToolButton::clicked, this, [this]() {
        editItem(m_list->currentItem());
    });
    connect(m_deleteButton, &QToolButton::clicked, this, &DeadlineWidget::deleteSelected);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DeadlineWidget::editItem);
    connect(m_list, &QListWidget::itemChanged, this, &DeadlineWidget::onItemChanged);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        updateButtons();
        // Jump the calendar to the deadline so the surrounding month is visible
        if (item && m_filter->currentData().toInt() != SelectedDay) {
            const QSignalBlocker blocker(m_calendar);
            m_calendar->setSelectedDate(item->data(s_dateRole).toDate());
        }
    });

    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_list, nullptr, nullptr, Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &DeadlineWidget::deleteSelected);

    connect(m_store, &DeadlineStore::changed, this, &DeadlineWidget::refresh);

    // Overdue state and "today" change with the clock
    auto *clock = new QTimer(this);
    clock->setInterval(60 * 1000);
    connect(clock, &QTimer::timeout, this, &DeadlineWidget::refresh);
    clock->start();

    refresh();
}

void DeadlineWidget::addDeadline()
{
    const QDate selected = m_calendar->selectedDate();
    addDeadlineOn(selected.isValid() ? selected : QDate::currentDate());
}

void DeadlineWidget::refresh()
{
    updateCalendarFormats();
    populateList();
    updateSummary();
}

void DeadlineWidget::updateCalendarFormats()
{
    m_calendar->setDateTextFormat(QDate(), QTextCharFormat());

    QColor highlight = palette().color(QPalette::Highlight);
    highlight.setAlpha(70);

    QTextCharFormat todayFormat;
    todayFormat.setFontUnderline(true);
    m_calendar->setDateTextFormat(QDate::currentDate(), todayFormat);

    for (const Deadline &deadline : m_store->deadlines()) {
        if (deadline.done) {
            continue;
        }
        QTextCharFormat format = m_calendar->dateTextFormat(deadline.date);
        format.setFontWeight(QFont::Bold);
        format.setBackground(highlight);
        if (deadline.isOverdue()) {
            format.setForeground(s_overdueColor);
        }
        m_calendar->setDateTextFormat(deadline.date, format);
    }
}

void DeadlineWidget::populateList()
{
    const QString currentId = m_list->currentItem() ? m_list->currentItem()->data(s_idRole).toString() : QString();
    const Filter filter = Filter(m_filter->currentData().toInt());
    const QDate today = QDate::currentDate();
    const QLocale locale;

    m_populating = true;
    m_list->clear();

    for (const Deadline &deadline : m_store->deadlines()) {
        bool include = false;
        switch (filter) {
        case SelectedDay:
            include = deadline.date == m_calendar->selectedDate();
            break;
        case Upcoming:
            include = !deadline.done && deadline.date <= today.addDays(s_upcomingDays);
            break;
        case All:
            include = true;
            break;
        }
        if (!include) {
            continue;
        }

        QStringList parts;
        if (filter != SelectedDay) {
            if (deadline.date == today) {
                parts << i18n("Today");
            } else if (deadline.date == today.addDays(1)) {
                parts << i18n("Tomorrow");
            } else {
                parts << locale.toString(deadline.date, QStringLiteral("ddd d MMM"));
            }
        }
        if (deadline.time.isValid()) {
            parts << locale.toString(deadline.time, QLocale::ShortFormat);
        }
        const QString when = parts.join(QStringLiteral(", "));

        auto *item = new QListWidgetItem(when.isEmpty() ? deadline.title : QStringLiteral("%1 — %2").arg(when, deadline.title));
        item->setData(s_idRole, deadline.id);
        item->setData(s_dateRole, deadline.date);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(deadline.done ? Qt::Checked : Qt::Unchecked);
        if (!deadline.notes.isEmpty()) {
            item->setToolTip(deadline.notes);
        }
        if (deadline.done) {
            QFont font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        } else if (deadline.isOverdue()) {
            item->setForeground(s_overdueColor);
            item->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
        }
        m_list->addItem(item);

        if (deadline.id == currentId) {
            m_list->setCurrentItem(item);
        }
    }

    if (m_list->count() == 0) {
        auto *placeholder = new QListWidgetItem(filter == SelectedDay ? i18n("Nothing due on this day.") : i18n("Nothing coming up."));
        placeholder->setFlags(Qt::NoItemFlags);
        m_list->addItem(placeholder);
    }

    m_populating = false;
    updateButtons();
}

void DeadlineWidget::updateSummary()
{
    const QDate today = QDate::currentDate();
    int overdue = 0;
    int dueToday = 0;
    for (const Deadline &deadline : m_store->deadlines()) {
        if (deadline.isOverdue()) {
            ++overdue;
        } else if (!deadline.done && deadline.date == today) {
            ++dueToday;
        }
    }

    QStringList parts;
    if (overdue > 0) {
        parts << QStringLiteral("<span style=\"color:%1\"><b>%2</b></span>").arg(s_overdueColor.name(), i18np("1 overdue", "%1 overdue", overdue));
    }
    if (dueToday > 0) {
        parts << QStringLiteral("<b>%1</b>").arg(i18np("1 due today", "%1 due today", dueToday));
    }
    m_summary->setText(parts.join(QStringLiteral(" · ")));
    m_summary->setVisible(!parts.isEmpty());
}

void DeadlineWidget::updateButtons()
{
    const QListWidgetItem *item = m_list->currentItem();
    const bool hasDeadline = item && !item->data(s_idRole).toString().isEmpty();
    m_editButton->setEnabled(hasDeadline);
    m_deleteButton->setEnabled(hasDeadline);
}

void DeadlineWidget::addDeadlineOn(const QDate &date)
{
    Deadline deadline;
    deadline.date = date;
    DeadlineDialog dialog(deadline, true, window());
    if (dialog.exec() == QDialog::Accepted) {
        m_store->add(dialog.deadline());
    }
}

void DeadlineWidget::editItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const Deadline deadline = m_store->find(item->data(s_idRole).toString());
    if (deadline.id.isEmpty()) {
        return;
    }
    DeadlineDialog dialog(deadline, false, window());
    if (dialog.exec() == QDialog::Accepted) {
        m_store->update(dialog.deadline());
    }
}

void DeadlineWidget::deleteSelected()
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const Deadline deadline = m_store->find(item->data(s_idRole).toString());
    if (deadline.id.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(window(), i18n("Delete Deadline"), i18n("Delete \"%1\"?", deadline.title));
    if (answer == QMessageBox::Yes) {
        m_store->remove(deadline.id);
    }
}

void DeadlineWidget::onItemChanged(QListWidgetItem *item)
{
    if (m_populating) {
        return;
    }
    const QString id = item->data(s_idRole).toString();
    const bool done = item->checkState() == Qt::Checked;
    // The store rebuilds the list, so defer until Qt has finished with this item
    QTimer::singleShot(0, this, [this, id, done]() {
        m_store->setDone(id, done);
    });
}
