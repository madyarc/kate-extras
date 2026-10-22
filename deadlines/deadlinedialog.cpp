#include "deadlinedialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimeEdit>

DeadlineDialog::DeadlineDialog(const Deadline &deadline, bool isNew, QWidget *parent)
    : QDialog(parent)
    , m_deadline(deadline)
    , m_titleEdit(new QLineEdit(deadline.title, this))
    , m_dateEdit(new QDateEdit(deadline.date, this))
    , m_timeCheck(new QCheckBox(i18n("At"), this))
    , m_timeEdit(new QTimeEdit(deadline.time.isValid() ? deadline.time : QTime(17, 0), this))
    , m_notesEdit(new QPlainTextEdit(deadline.notes, this))
{
    setWindowTitle(isNew ? i18n("New Deadline") : i18n("Edit Deadline"));
    setMinimumWidth(380);

    m_titleEdit->setPlaceholderText(i18n("What is due?"));
    m_dateEdit->setCalendarPopup(true);
    m_timeCheck->setChecked(deadline.time.isValid());
    m_timeEdit->setEnabled(deadline.time.isValid());
    connect(m_timeCheck, &QCheckBox::toggled, m_timeEdit, &QWidget::setEnabled);
    m_notesEdit->setPlaceholderText(i18n("Optional notes"));

    auto *timeLayout = new QHBoxLayout;
    timeLayout->addWidget(m_timeCheck);
    timeLayout->addWidget(m_timeEdit, 1);

    auto *form = new QFormLayout;
    form->addRow(i18n("Title:"), m_titleEdit);
    form->addRow(i18n("Date:"), m_dateEdit);
    form->addRow(i18n("Time:"), timeLayout);
    form->addRow(i18n("Notes:"), m_notesEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(!deadline.title.trimmed().isEmpty());
    connect(m_titleEdit, &QLineEdit::textChanged, okButton, [okButton](const QString &text) {
        okButton->setEnabled(!text.trimmed().isEmpty());
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    m_titleEdit->setFocus();
}

Deadline DeadlineDialog::deadline() const
{
    Deadline result = m_deadline;
    result.title = m_titleEdit->text().trimmed();
    result.date = m_dateEdit->date();
    result.time = m_timeCheck->isChecked() ? m_timeEdit->time() : QTime();
    result.notes = m_notesEdit->toPlainText().trimmed();
    return result;
}
