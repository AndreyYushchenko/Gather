#include "TextSlideWidget.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

TextSlideWidget::TextSlideWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void TextSlideWidget::setText(const QString &text, const QString &reference, bool chords,
                            const DisplaySettings::TextStyle &style, double starsGapLines, const QStringList &fitGroup)
{
    m_starsGapLines = starsGapLines;
    m_fitGroup = fitGroup;
    m_text = text;
    m_reference = reference;
    m_chords = chords;
    m_style = style;
    m_cache = QPixmap();
    update();
}

void TextSlideWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_cache = QPixmap();
}

void TextSlideWidget::paintEvent(QPaintEvent *)
{
    if (width() < 1 || height() < 1) return;
    const qreal dpr = devicePixelRatioF();
    if (m_cache.isNull()) {
        m_cache = QPixmap(size() * dpr);
        m_cache.setDevicePixelRatio(dpr);
        m_cache.fill(Qt::transparent);
        QPainter cachePainter(&m_cache);
        drawText(cachePainter);
    }
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_cache);
}

void TextSlideWidget::drawText(QPainter &painter)
{
    const qreal scale = width() / 1920.0;
    const qreal canvasWidth = width() / scale;
    const qreal canvasHeight = height() / scale;
    const qreal textWidth = canvasWidth * qMin(m_style.maxWidth, 100 - 2 * m_style.margin) / 100;
    const qreal textHeight = canvasHeight * (100 - 2 * m_style.margin) / 100;
    QTextDocument doc;
    doc.setDocumentMargin(0);
    const Qt::Alignment align = m_style.alignment == QLatin1String("left") ? Qt::AlignLeft
        : m_style.alignment == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter;
    const auto layout = [&](const QString &source, const QString &reference, int pixels) {
        doc.clear();
        QFont font(m_style.family);
        font.setPixelSize(pixels);
        font.setWeight(QFont::Weight(m_style.weight));
        font.setLetterSpacing(m_style.bible ? QFont::PercentageSpacing : QFont::AbsoluteSpacing,
            m_style.bible ? 100 + m_style.letterSpacing : m_style.letterSpacing * pixels / m_style.fontSize);
        doc.setDefaultFont(font);
        QTextOption option;
        option.setAlignment(align);
        option.setWrapMode(m_style.wordWrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        doc.setDefaultTextOption(option);
        QString text = DisplaySettings::transformText(source, m_style);
        if (m_chords) {
            text = text.toHtmlEscaped();
            static const QRegularExpression chord(QStringLiteral(R"(\[([^\]\s]{1,8})\])"));
            text.replace(chord, QStringLiteral("<sup style='color:#ffd166;'>\\1</sup>"));
            doc.setHtml(text.replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        } else {
            doc.setPlainText(text);
        }
        QTextCursor cursor(&doc);
        cursor.select(QTextCursor::Document);
        QTextCharFormat chars;
        chars.setForeground(Qt::white);
        if (m_style.outline) chars.setTextOutline(QPen(Qt::black, m_style.outlineWidth * pixels / qreal(m_style.fontSize), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        cursor.mergeCharFormat(chars);
        QTextBlockFormat block;
        block.setAlignment(align);
        block.setLineHeight(m_style.lineHeight, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(block);
        // The "***" line: its own gap instead of a whole blank line. (With
        // chords the lines are <br>s of one block and it just follows on.)
        if (m_starsGapLines >= 0 && doc.lastBlock().text().trimmed() == QLatin1String("***")) {
            QTextCursor stars(doc.lastBlock());
            QTextBlockFormat format = stars.blockFormat();
            format.setTopMargin(pixels * m_style.lineHeight / 100.0 * m_starsGapLines);
            stars.setBlockFormat(format);
        }
        if (!reference.isEmpty()) {
            cursor.movePosition(QTextCursor::End);
            block.setTopMargin(pixels * 0.6);
            cursor.insertBlock(block);
            chars.setProperty(QTextFormat::FontPixelSize, qMax(12.0, pixels * 0.4));
            chars.setFontWeight(QFont::Normal);
            cursor.insertText(reference, chars);
        }
        doc.setTextWidth(textWidth);
    };
    const auto laidOutLines = [&] {
        int count = 0;
        for (QTextBlock block = doc.begin(); block.isValid(); block = block.next())
            count += block.layout() ? block.layout()->lineCount() : 1;
        return count;
    };
    const auto fitsHeight = [&] { return doc.size().height() <= textHeight && doc.idealWidth() <= textWidth + 1; };
    // Songs keep every line whole, as written in the songbook, as long as
    // that doesn't take the text below a readable size; only past that is a
    // line allowed to wrap. Bible verses are prose and simply wrap.
    const bool keepLines = !m_style.bible && m_style.wordWrap;
    const int keepLinesFloor = qMax(28, m_style.fontSize * 35 / 100);
    // The largest size (up to the set one) at which `source` fits.
    const auto fittedSize = [&](const QString &source, const QString &reference) {
        const int pixels = m_style.fontSize;
        if (!m_style.autoFit)
            return pixels;
        // Lines the text has of its own (plus the reference line): more
        // laid-out lines than that means some line had to wrap.
        const int ownLines = int(source.count(QLatin1Char('\n'))) + 1 + (reference.isEmpty() ? 0 : 1);
        const auto fits = [&](int size) {
            layout(source, reference, size);
            return fitsHeight() && (!keepLines || size <= keepLinesFloor || laidOutLines() <= ownLines);
        };
        if (fits(pixels))
            return pixels;
        int low = 12, high = pixels - 1, best = 12;
        while (low <= high) {
            const int candidate = (low + high) / 2;
            if (fits(candidate)) { best = candidate; low = candidate + 1; }
            else high = candidate - 1;
        }
        return best;
    };
    int pixels = fittedSize(m_text, m_reference);
    if (m_style.autoFit && !m_fitGroup.isEmpty()) {
        // One size for the whole song: the one its longest slide needs.
        // Computed once per song and style, not on every slide change.
        const QString key = m_fitGroup.join(QChar(0x1e)) + QChar(0x1f)
            + QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10").arg(m_style.family).arg(m_style.fontSize).arg(m_style.weight)
                  .arg(m_style.letterSpacing).arg(m_style.lineHeight).arg(m_style.textCase).arg(m_style.margin).arg(m_style.maxWidth)
                  .arg(m_style.wordWrap).arg(m_chords);
        if (key != m_groupSizeKey) {
            m_groupSizeKey = key;
            m_groupSize = m_style.fontSize;
            for (const QString &other : std::as_const(m_fitGroup))
                m_groupSize = qMin(m_groupSize, fittedSize(other, m_reference));
        }
        pixels = qMin(pixels, m_groupSize);
    }
    layout(m_text, m_reference, pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.setClipRect(QRectF((canvasWidth - textWidth) / 2, (canvasHeight - textHeight) / 2, textWidth, textHeight));
    painter.translate((canvasWidth - textWidth) / 2, qMax((canvasHeight - textHeight) / 2, (canvasHeight - doc.size().height()) / 2));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, Qt::white);
    if (m_style.shadow) {
        QAbstractTextDocumentLayout::PaintContext shadow;
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = QTextCursor(&doc);
        selection.cursor.select(QTextCursor::Document);
        selection.format.setForeground(QColor(0, 0, 0, qRound(m_style.shadowOpacity * 2.55 / 4)));
        selection.format.setTextOutline(QPen(Qt::NoPen));
        shadow.selections << selection;
        for (int x : {-3, 0, 3}) for (int y : {0, 3, 6}) {
            painter.save(); painter.translate(x, y);
            doc.documentLayout()->draw(&painter, shadow);
            painter.restore();
        }
    }
    doc.documentLayout()->draw(&painter, context);
}
