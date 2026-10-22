#pragma once

#include "deadlinestore.h"

#include <QDialog>

class QCheckBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;
class QTimeEdit;

class DeadlineDialog : public QDialog
{
    Q_OBJECT

public:
    DeadlineDialog(const Deadline &deadline, bool isNew, QWidget *parent = nullptr);

    Deadline deadline() const;

private:
    Deadline m_deadline;
    QLineEdit *m_titleEdit;
    QDateEdit *m_dateEdit;
    QCheckBox *m_timeCheck;
    QTimeEdit *m_timeEdit;
    QPlainTextEdit *m_notesEdit;
};
