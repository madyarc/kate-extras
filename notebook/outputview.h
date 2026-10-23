#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QTextBrowser>
#include <QWidget>

class QVBoxLayout;

// A read-only rich text view that grows to fit its content instead of scrolling, up to
// an optional maximum height (long logs scroll inside so they do not swamp the notebook)
class AutoHeightBrowser : public QTextBrowser
{
    Q_OBJECT

public:
    explicit AutoHeightBrowser(QWidget *parent = nullptr, int maximumHeight = 0);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitToContent();

    int m_maximumHeight;
};

// The outputs of one code cell, rendered the way Jupyter shows them
class OutputView : public QWidget
{
    Q_OBJECT

public:
    explicit OutputView(QWidget *parent = nullptr);

    void setOutputs(const QJsonArray &outputs);
    // A new output from the kernel. Consecutive stream outputs are merged, as Jupyter does.
    void append(const QJsonObject &output);
    void clear();
    // clear_output(wait=True): clear just before the next output arrives
    void clearOnNextOutput();

    QJsonArray outputs() const;

private:
    QWidget *renderOutput(const QJsonObject &output);
    QWidget *renderStream(const QJsonObject &output);
    QWidget *renderError(const QJsonObject &output);
    QWidget *renderData(const QJsonObject &output);

    QVBoxLayout *m_layout;
    QJsonArray m_outputs;
    QList<QWidget *> m_widgets;
    bool m_clearPending = false;
};
