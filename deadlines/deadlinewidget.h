#pragma once

#include <QDate>
#include <QWidget>

class DeadlineStore;
class QCalendarWidget;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QToolButton;

class DeadlineWidget : public QWidget
{
    Q_OBJECT

public:
    DeadlineWidget(DeadlineStore *store, QWidget *parent = nullptr);

public Q_SLOTS:
    void addDeadline();

private:
    enum Filter {
        SelectedDay,
        Upcoming,
        All,
    };

    void refresh();
    void updateCalendarFormats();
    void populateList();
    void updateSummary();
    void updateButtons();
    void addDeadlineOn(const QDate &date);
    void editItem(QListWidgetItem *item);
    void deleteSelected();
    void onItemChanged(QListWidgetItem *item);

    DeadlineStore *m_store;
    QCalendarWidget *m_calendar;
    QComboBox *m_filter;
    QLabel *m_summary;
    QListWidget *m_list;
    QToolButton *m_addButton;
    QToolButton *m_editButton;
    QToolButton *m_deleteButton;
    bool m_populating = false;
};
