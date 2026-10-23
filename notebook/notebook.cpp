#include "notebook.h"

#include <KLocalizedString>

#include <QJsonDocument>
#include <QLocale>
#include <QRandomGenerator>

namespace
{
// nbformat stores multi-line strings either whole or as a list of lines
QString joinLines(const QJsonValue &value)
{
    if (!value.isArray()) {
        return value.toString();
    }
    QString text;
    for (const QJsonValue &line : value.toArray()) {
        text += line.toString();
    }
    return text;
}

// Python's str.splitlines(keepends=True), restricted to '\n' like nbformat
QJsonArray splitLines(const QString &text)
{
    QJsonArray lines;
    qsizetype start = 0;
    while (start < text.size()) {
        const qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0) {
            lines.append(text.mid(start));
            break;
        }
        lines.append(text.mid(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}

// nbformat's split_lines(): which output values are stored as lists of lines
bool isLineSplitMime(const QString &mime)
{
    return mime.startsWith(QLatin1String("text/")) || mime == QLatin1String("image/svg+xml") || mime == QLatin1String("application/javascript");
}

QJsonArray normalizedOutputs(const QJsonArray &outputs)
{
    QJsonArray result;
    for (const QJsonValue &value : outputs) {
        QJsonObject output = value.toObject();
        const QString type = output.value(QLatin1String("output_type")).toString();
        if (type == QLatin1String("stream")) {
            output.insert(QStringLiteral("text"), splitLines(joinLines(output.value(QLatin1String("text")))));
        } else if (type == QLatin1String("display_data") || type == QLatin1String("execute_result")) {
            QJsonObject data = output.value(QLatin1String("data")).toObject();
            for (auto it = data.begin(); it != data.end(); ++it) {
                if (isLineSplitMime(it.key()) && (it->isString() || it->isArray())) {
                    *it = splitLines(joinLines(*it));
                }
            }
            output.insert(QStringLiteral("data"), data);
            if (!output.contains(QLatin1String("metadata"))) {
                output.insert(QStringLiteral("metadata"), QJsonObject());
            }
        }
        result.append(output);
    }
    return result;
}

QString cellId()
{
    return QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, QLatin1Char('0'));
}

// json.dumps(..., indent=1, sort_keys=True, ensure_ascii=False), which is how Jupyter saves
void writeString(QByteArray &out, const QString &string)
{
    out += '"';
    // Copy runs of ordinary characters in one go, so surrogate pairs (emoji) stay intact
    qsizetype runStart = 0;
    for (qsizetype i = 0; i < string.size(); ++i) {
        const QChar c = string.at(i);
        if (c.unicode() >= 0x20 && c != QLatin1Char('"') && c != QLatin1Char('\\')) {
            continue;
        }
        out += QStringView(string).mid(runStart, i - runStart).toUtf8();
        runStart = i + 1;
        switch (c.unicode()) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            out += "\\u" + QByteArray::number(c.unicode(), 16).rightJustified(4, '0');
        }
    }
    out += QStringView(string).mid(runStart).toUtf8();
    out += '"';
}

void writeValue(QByteArray &out, const QJsonValue &value, int depth, int indentWidth)
{
    const QByteArray indent((depth + 1) * indentWidth, ' ');
    const QByteArray closingIndent(depth * indentWidth, ' ');

    switch (value.type()) {
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        bool first = true;
        // QJsonObject keeps its keys sorted, matching sort_keys=True
        for (auto it = object.begin(); it != object.end(); ++it) {
            out += first ? "" : ",\n";
            first = false;
            out += indent;
            writeString(out, it.key());
            out += ": ";
            writeValue(out, *it, depth + 1, indentWidth);
        }
        out += '\n' + closingIndent + '}';
        return;
    }
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (qsizetype i = 0; i < array.size(); ++i) {
            out += i ? ",\n" : "";
            out += indent;
            writeValue(out, array.at(i), depth + 1, indentWidth);
        }
        out += '\n' + closingIndent + ']';
        return;
    }
    case QJsonValue::String:
        writeString(out, value.toString());
        return;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (number == double(qint64(number)) && qAbs(number) < 9007199254740992.0) {
            out += QByteArray::number(qint64(number));
        } else {
            out += QLocale::c().toString(number, 'g', QLocale::FloatingPointShortest).toLatin1();
        }
        return;
    }
    case QJsonValue::Bool:
        out += value.toBool() ? "true" : "false";
        return;
    default:
        out += "null";
    }
}
}

bool Notebook::load(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = i18n("The file is not valid JSON: %1", parseError.errorString());
        return false;
    }

    m_root = document.object();

    // Jupyter indents by one space; keep whatever the file uses (Colab writes two)
    // so that saving does not rewrite every line
    const qsizetype firstLine = json.indexOf('\n');
    if (firstLine >= 0) {
        qsizetype spaces = 0;
        while (firstLine + 1 + spaces < json.size() && json.at(firstLine + 1 + spaces) == ' ') {
            ++spaces;
        }
        m_indentWidth = spaces > 0 ? int(spaces) : 1;
    }

    const int format = m_root.value(QLatin1String("nbformat")).toInt();
    if (format != 4) {
        *error = i18n("Only notebook format 4 is supported; this file uses format %1. Re-save it with a current Jupyter first.", format);
        return false;
    }

    cells.clear();
    const QJsonArray jsonCells = m_root.take(QLatin1String("cells")).toArray();
    for (const QJsonValue &value : jsonCells) {
        QJsonObject object = value.toObject();
        NotebookCell cell;
        cell.type = object.take(QLatin1String("cell_type")).toString();
        cell.source = joinLines(object.take(QLatin1String("source")));
        cell.outputs = object.take(QLatin1String("outputs")).toArray();
        cell.executionCount = object.take(QLatin1String("execution_count"));
        if (cell.executionCount.isUndefined()) {
            cell.executionCount = QJsonValue::Null;
        }
        cell.extra = object;
        cells.append(cell);
    }
    return true;
}

QByteArray Notebook::toJson() const
{
    QJsonArray jsonCells;
    for (const NotebookCell &cell : cells) {
        QJsonObject object = cell.extra;
        object.insert(QStringLiteral("cell_type"), cell.type);
        object.insert(QStringLiteral("source"), splitLines(cell.source));
        if (!object.contains(QLatin1String("metadata"))) {
            object.insert(QStringLiteral("metadata"), QJsonObject());
        }
        if (cell.type == QLatin1String("code")) {
            object.insert(QStringLiteral("outputs"), normalizedOutputs(cell.outputs));
            object.insert(QStringLiteral("execution_count"), cell.executionCount);
        }
        jsonCells.append(object);
    }

    QJsonObject root = m_root;
    root.insert(QStringLiteral("cells"), jsonCells);

    QByteArray out;
    writeValue(out, root, 0, m_indentWidth);
    out += '\n';
    return out;
}

QString Notebook::language() const
{
    const QJsonObject metadata = m_root.value(QLatin1String("metadata")).toObject();
    const QString language = metadata.value(QLatin1String("language_info")).toObject().value(QLatin1String("name")).toString();
    if (!language.isEmpty()) {
        return language;
    }
    return metadata.value(QLatin1String("kernelspec")).toObject().value(QLatin1String("language")).toString(QStringLiteral("python"));
}

QString Notebook::kernelName() const
{
    const QJsonObject kernelspec = m_root.value(QLatin1String("metadata")).toObject().value(QLatin1String("kernelspec")).toObject();
    return kernelspec.value(QLatin1String("name")).toString(QStringLiteral("python3"));
}

NotebookCell Notebook::newCell(const QString &type) const
{
    NotebookCell cell;
    cell.type = type;
    cell.extra.insert(QStringLiteral("metadata"), QJsonObject());
    // Cell ids arrived with nbformat 4.5
    if (m_root.value(QLatin1String("nbformat_minor")).toInt() >= 5) {
        cell.extra.insert(QStringLiteral("id"), cellId());
    }
    return cell;
}
