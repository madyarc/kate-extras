#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// One cell as the editor works with it. Everything the editor does not touch (id,
// metadata, attachments, unknown keys) stays in `extra` and is written back unchanged.
struct NotebookCell {
    QString type; // "code", "markdown" or "raw"
    QString source;
    QJsonArray outputs; // code cells only
    QJsonValue executionCount = QJsonValue::Null;
    QJsonObject extra;
};

// A Jupyter notebook (nbformat 4) on disk
class Notebook
{
public:
    bool load(const QByteArray &json, QString *error);
    // Serialised the way Jupyter itself writes notebooks, so saving does not churn diffs
    QByteArray toJson() const;

    // Language and kernel the notebook was last run with, e.g. "python" / "python3"
    QString language() const;
    QString kernelName() const;

    NotebookCell newCell(const QString &type) const;

    QList<NotebookCell> cells;

private:
    QJsonObject m_root; // the file minus "cells"
    int m_indentWidth = 1;
};
