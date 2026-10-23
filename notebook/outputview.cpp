#include "outputview.h"

#include <KTextEditor/Editor>

#include <QAbstractTextDocumentLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QtMath>

namespace
{
// Taller text outputs scroll inside the output instead of stretching the notebook
constexpr int s_maximumTextHeight = 420;

const QString s_stdoutStyle = QStringLiteral("white-space: pre-wrap;");

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

// A carriage return rewinds to the start of the line, which is how progress bars
// (tqdm, pip) redraw themselves; keep only what the terminal would end up showing
QString applyCarriageReturns(const QString &text)
{
    if (!text.contains(QLatin1Char('\r'))) {
        return text;
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const qsizetype rewind = line.lastIndexOf(QLatin1Char('\r'));
        if (rewind >= 0) {
            line = line.mid(rewind + 1);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

// The 16 ANSI colours as Jupyter draws them; readable on light and dark backgrounds
const char *const s_ansiColors[16] = {
    "#3e424d", "#e75c58", "#00a250", "#ddb62b", "#208ffb", "#d160c4", "#60c6c8", "#c5c1b4",
    "#282c36", "#b22b31", "#007427", "#b27d12", "#0065ca", "#a03196", "#258f8f", "#a1a6b2",
};

QString xtermColor(int index)
{
    if (index < 16) {
        return QLatin1String(s_ansiColors[index]);
    }
    if (index < 232) {
        static const int levels[6] = {0, 95, 135, 175, 215, 255};
        const int value = index - 16;
        return QColor(levels[value / 36], levels[(value / 6) % 6], levels[value % 6]).name();
    }
    const int grey = 8 + (index - 232) * 10;
    return QColor(grey, grey, grey).name();
}

// Tracebacks arrive with ANSI SGR colour codes; turn them into HTML spans
QString ansiToHtml(const QString &text)
{
    QString html;
    QString foreground, background;
    bool bold = false;
    bool spanOpen = false;

    auto openSpan = [&]() {
        if (spanOpen) {
            html += QStringLiteral("</span>");
            spanOpen = false;
        }
        QStringList style;
        if (!foreground.isEmpty()) {
            style << QStringLiteral("color:%1").arg(foreground);
        }
        if (!background.isEmpty()) {
            style << QStringLiteral("background-color:%1").arg(background);
        }
        if (bold) {
            style << QStringLiteral("font-weight:bold");
        }
        if (!style.isEmpty()) {
            html += QStringLiteral("<span style=\"%1\">").arg(style.join(QLatin1Char(';')));
            spanOpen = true;
        }
    };

    qsizetype i = 0;
    while (i < text.size()) {
        const qsizetype escape = text.indexOf(QStringLiteral("\x1b["), i);
        const qsizetype plainEnd = escape < 0 ? text.size() : escape;
        html += text.mid(i, plainEnd - i).toHtmlEscaped();
        if (escape < 0) {
            break;
        }

        qsizetype end = escape + 2;
        while (end < text.size() && !text.at(end).isLetter()) {
            ++end;
        }
        if (end >= text.size()) {
            break;
        }
        if (text.at(end) == QLatin1Char('m')) {
            const QStringList codes = text.mid(escape + 2, end - escape - 2).split(QLatin1Char(';'));
            for (qsizetype c = 0; c < codes.size(); ++c) {
                const int code = codes.at(c).isEmpty() ? 0 : codes.at(c).toInt();
                if (code == 0) {
                    foreground.clear();
                    background.clear();
                    bold = false;
                } else if (code == 1) {
                    bold = true;
                } else if (code == 22) {
                    bold = false;
                } else if (code >= 30 && code <= 37) {
                    foreground = xtermColor(code - 30);
                } else if (code >= 90 && code <= 97) {
                    foreground = xtermColor(code - 90 + 8);
                } else if (code == 39) {
                    foreground.clear();
                } else if (code >= 40 && code <= 47) {
                    background = xtermColor(code - 40);
                } else if (code >= 100 && code <= 107) {
                    background = xtermColor(code - 100 + 8);
                } else if (code == 49) {
                    background.clear();
                } else if ((code == 38 || code == 48) && c + 1 < codes.size()) {
                    QString color;
                    if (codes.at(c + 1) == QLatin1String("5") && c + 2 < codes.size()) {
                        color = xtermColor(codes.at(c + 2).toInt());
                        c += 2;
                    } else if (codes.at(c + 1) == QLatin1String("2") && c + 4 < codes.size()) {
                        color = QColor(codes.at(c + 2).toInt(), codes.at(c + 3).toInt(), codes.at(c + 4).toInt()).name();
                        c += 4;
                    }
                    (code == 38 ? foreground : background) = color;
                }
            }
            openSpan();
        }
        i = end + 1;
    }
    if (spanOpen) {
        html += QStringLiteral("</span>");
    }
    return html;
}

QString preformatted(const QString &innerHtml)
{
    return QStringLiteral("<pre style=\"%1\">%2</pre>").arg(s_stdoutStyle, innerHtml);
}

QFont monospaceFont()
{
    return KTextEditor::Editor::instance()->font();
}

// Images are shown at their natural size, scaled down to fit narrower panels
class ImageLabel : public QLabel
{
public:
    ImageLabel(const QPixmap &pixmap, QWidget *parent)
        : QLabel(parent)
        , m_pixmap(pixmap)
    {
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        updatePixmap();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

private:
    void updatePixmap()
    {
        const QSizeF natural = m_pixmap.deviceIndependentSize();
        const qreal scale = width() > 0 && natural.width() > width() ? width() / natural.width() : 1.0;
        const QSize size = (natural * scale).toSize();
        if (size == m_shownSize) {
            return;
        }
        m_shownSize = size;
        const qreal dpr = devicePixelRatioF();
        QPixmap scaled = m_pixmap.scaled(size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        setPixmap(scaled);
        setFixedHeight(size.height());
    }

    QPixmap m_pixmap;
    QSize m_shownSize;
};

QPixmap pixmapFromData(const QJsonObject &data, const QJsonObject &metadata, qreal dpr)
{
    for (const QString &mime : {QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/gif")}) {
        if (!data.contains(mime)) {
            continue;
        }
        QPixmap pixmap;
        pixmap.loadFromData(QByteArray::fromBase64(joinLines(data.value(mime)).toLatin1()));
        // IPython's retina images say how large they are meant to be shown
        const QJsonObject imageMetadata = metadata.value(mime).toObject();
        const int width = imageMetadata.value(QLatin1String("width")).toInt();
        if (!pixmap.isNull() && width > 0 && width < pixmap.width()) {
            pixmap.setDevicePixelRatio(qreal(pixmap.width()) / width);
        }
        return pixmap;
    }

    if (data.contains(QLatin1String("image/svg+xml"))) {
        QSvgRenderer renderer(joinLines(data.value(QLatin1String("image/svg+xml"))).toUtf8());
        if (renderer.isValid()) {
            const QSize size = renderer.defaultSize();
            QPixmap pixmap(size * dpr);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            painter.end();
            pixmap.setDevicePixelRatio(dpr);
            return pixmap;
        }
    }
    return {};
}
}

AutoHeightBrowser::AutoHeightBrowser(QWidget *parent, int maximumHeight)
    : QTextBrowser(parent)
    , m_maximumHeight(maximumHeight)
{
    setFrameShape(QFrame::NoFrame);
    setOpenExternalLinks(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(maximumHeight > 0 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    viewport()->setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; }"));
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &AutoHeightBrowser::fitToContent);
}

QSize AutoHeightBrowser::sizeHint() const
{
    return QSize(QTextBrowser::sizeHint().width(), height());
}

QSize AutoHeightBrowser::minimumSizeHint() const
{
    return QSize(0, height());
}

void AutoHeightBrowser::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    fitToContent();
}

void AutoHeightBrowser::fitToContent()
{
    document()->setTextWidth(viewport()->width());
    int height = qCeil(document()->size().height()) + 2 * frameWidth();
    if (m_maximumHeight > 0) {
        height = qMin(height, m_maximumHeight);
    }
    if (height != this->height()) {
        setFixedHeight(height);
    }
}

OutputView::OutputView(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    setVisible(false);
}

void OutputView::setOutputs(const QJsonArray &outputs)
{
    clear();
    for (const QJsonValue &output : outputs) {
        append(output.toObject());
    }
}

void OutputView::append(const QJsonObject &output)
{
    if (m_clearPending) {
        clear();
    }

    // Jupyter joins consecutive writes to the same stream into one output
    if (output.value(QLatin1String("output_type")) == QLatin1String("stream") && !m_outputs.isEmpty()) {
        QJsonObject last = m_outputs.last().toObject();
        if (last.value(QLatin1String("output_type")) == QLatin1String("stream") && last.value(QLatin1String("name")) == output.value(QLatin1String("name"))) {
            last.insert(QStringLiteral("text"),
                        applyCarriageReturns(joinLines(last.value(QLatin1String("text"))) + joinLines(output.value(QLatin1String("text")))));
            m_outputs.replace(m_outputs.size() - 1, last);
            delete m_widgets.takeLast();
            m_widgets.append(renderOutput(last));
            m_layout->addWidget(m_widgets.last());
            return;
        }
    }

    QJsonObject stored = output;
    if (output.value(QLatin1String("output_type")) == QLatin1String("stream")) {
        stored.insert(QStringLiteral("text"), applyCarriageReturns(joinLines(output.value(QLatin1String("text")))));
    }
    m_outputs.append(stored);
    m_widgets.append(renderOutput(stored));
    m_layout->addWidget(m_widgets.last());
    setVisible(true);
}

void OutputView::clear()
{
    m_clearPending = false;
    qDeleteAll(m_widgets);
    m_widgets.clear();
    m_outputs = QJsonArray();
    setVisible(false);
}

void OutputView::clearOnNextOutput()
{
    m_clearPending = true;
}

QJsonArray OutputView::outputs() const
{
    return m_outputs;
}

QWidget *OutputView::renderOutput(const QJsonObject &output)
{
    const QString type = output.value(QLatin1String("output_type")).toString();
    if (type == QLatin1String("stream")) {
        return renderStream(output);
    }
    if (type == QLatin1String("error")) {
        return renderError(output);
    }
    return renderData(output);
}

QWidget *OutputView::renderStream(const QJsonObject &output)
{
    auto *browser = new AutoHeightBrowser(this, s_maximumTextHeight);
    browser->setFont(monospaceFont());
    // Text written to stderr may carry colour codes too (warnings, logging)
    browser->setHtml(preformatted(ansiToHtml(joinLines(output.value(QLatin1String("text"))))));
    if (output.value(QLatin1String("name")) == QLatin1String("stderr")) {
        browser->setStyleSheet(QStringLiteral("QTextBrowser { background: rgba(231, 92, 88, 0.12); }"));
    }
    browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
    return browser;
}

QWidget *OutputView::renderError(const QJsonObject &output)
{
    QStringList lines;
    for (const QJsonValue &line : output.value(QLatin1String("traceback")).toArray()) {
        lines << line.toString();
    }
    if (lines.isEmpty()) {
        lines << output.value(QLatin1String("ename")).toString() + QStringLiteral(": ") + output.value(QLatin1String("evalue")).toString();
    }
    auto *browser = new AutoHeightBrowser(this, s_maximumTextHeight);
    browser->setFont(monospaceFont());
    browser->setHtml(preformatted(ansiToHtml(lines.join(QLatin1Char('\n')))));
    browser->setStyleSheet(QStringLiteral("QTextBrowser { background: rgba(231, 92, 88, 0.12); }"));
    return browser;
}

QWidget *OutputView::renderData(const QJsonObject &output)
{
    const QJsonObject data = output.value(QLatin1String("data")).toObject();
    const QJsonObject metadata = output.value(QLatin1String("metadata")).toObject();

    const QPixmap pixmap = pixmapFromData(data, metadata, devicePixelRatioF());
    if (!pixmap.isNull()) {
        return new ImageLabel(pixmap, this);
    }

    auto *browser = new AutoHeightBrowser(this);
    if (data.contains(QLatin1String("text/html"))) {
        // pandas tables and IPython.display.HTML; Qt renders a useful subset of HTML.
        // pandas asks for border="1", which Qt draws as heavy double lines.
        browser->document()->setDefaultStyleSheet(
            QStringLiteral("table { border-collapse: collapse; } th, td { border: 1px solid rgba(128, 128, 128, 0.5); padding: 2px 8px; }"));
        browser->setHtml(joinLines(data.value(QLatin1String("text/html"))));
    } else if (data.contains(QLatin1String("text/markdown"))) {
        browser->setMarkdown(joinLines(data.value(QLatin1String("text/markdown"))));
    } else {
        QString text = joinLines(data.value(QLatin1String("text/plain")));
        if (text.isEmpty() && data.contains(QLatin1String("text/latex"))) {
            text = joinLines(data.value(QLatin1String("text/latex")));
        }
        browser->setFont(monospaceFont());
        browser->setHtml(preformatted(ansiToHtml(text)));
    }
    return browser;
}
