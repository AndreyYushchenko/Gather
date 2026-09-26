#include "PlaylistsPanel.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "TimerWidgets.h"
#include "VideoThumbnailer.h"
#include "core/ContentRepository.h"
#include "core/Database.h"

#include <QAbstractItemModel>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace TimerUi;

namespace {


// design.pen "Type Badge" colours per content type.
struct TypeStyle {
    QString icon;
    QString foreground;
    QString background;
    QString label;
};

TypeStyle typeStyle(ContentType type)
{
    switch (type) {
    case ContentType::Song: return {QStringLiteral("music"), QStringLiteral("#2f6fed"), QStringLiteral("#e8f0fe"), QObject::tr("Песня")};
    case ContentType::BibleVerse:
        return {QStringLiteral("book-open"), QStringLiteral("#7c3aed"), QStringLiteral("#f1eafe"), QObject::tr("Библия")};
    case ContentType::Announcement:
        return {QStringLiteral("megaphone"), QStringLiteral("#16a34a"), QStringLiteral("#e6f6ec"), QObject::tr("Объявление")};
    case ContentType::Video: return {QStringLiteral("video"), QStringLiteral("#dc2626"), QStringLiteral("#fdeaea"), QObject::tr("Видео")};
    case ContentType::Photo: return {QStringLiteral("image"), QStringLiteral("#ea7a0c"), QStringLiteral("#fdf1e3"), QObject::tr("Фото")};
    }
    return {QStringLiteral("music"), QStringLiteral("#2f6fed"), QStringLiteral("#e8f0fe"), QString()};
}

bool isRemote(const QString &path)
{
    return path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"));
}

// The picture that stands for an item: the photo itself, a video's poster
// frame, or a slide's background. Null while loading or when there's none.
QPixmap itemPixmap(const ContentItem &item)
{
    if (item.type == ContentType::Photo && !item.imagePath.isEmpty())
        return ThumbnailCache::instance()->thumbnail(item.imagePath);
    if (item.type == ContentType::Video && !item.imagePath.isEmpty() && !isRemote(item.imagePath))
        return VideoThumbnailer::instance()->poster(item.imagePath);
    if (item.backgroundType == BackgroundType::Photo && !item.backgroundPath.isEmpty())
        return ThumbnailCache::instance()->thumbnail(item.backgroundPath);
    if (item.backgroundType == BackgroundType::Video && !item.backgroundPath.isEmpty())
        return VideoThumbnailer::instance()->poster(item.backgroundPath);
    return QPixmap();
}

QPixmap coverPixmap(const Playlist &playlist, const QList<PlaylistEntry> &entries)
{
    if (!playlist.coverPath.isEmpty())
        return ThumbnailCache::instance()->thumbnail(playlist.coverPath);
    for (const PlaylistEntry &entry : entries) {
        const QPixmap pixmap = itemPixmap(entry.item);
        if (!pixmap.isNull())
            return pixmap;
    }
    return QPixmap();
}

QString formatClock(int seconds)
{
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// Planned length of one entry: the operator's value, else a local video's
// own length, else unknown (0).
int entrySeconds(const PlaylistEntry &entry)
{
    if (entry.durationSec > 0)
        return entry.durationSec;
    if (entry.item.type == ContentType::Video && !isRemote(entry.item.imagePath)) {
        const qint64 ms = VideoThumbnailer::instance()->duration(entry.item.imagePath);
        if (ms > 0)
            return int((ms + 999) / 1000);
    }
    return 0;
}

QString totalText(const QList<PlaylistEntry> &entries)
{
    int total = 0;
    for (const PlaylistEntry &entry : entries)
        total += entrySeconds(entry);
    return total > 0 ? QObject::tr("%1 мин").arg((total + 59) / 60) : QObject::tr("— мин");
}

QString plural(int n, const QString &one, const QString &few, const QString &many)
{
    const int mod10 = n % 10;
    const int mod100 = n % 100;
    if (mod10 == 1 && mod100 != 11)
        return one;
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
        return few;
    return many;
}

QString countText(int n)
{
    return QStringLiteral("%1 %2").arg(n).arg(plural(n, QObject::tr("элемент"), QObject::tr("элемента"), QObject::tr("элементов")));
}

QString dateText(const QDateTime &date)
{
    return QLocale().toString(date.date(), QObject::tr("d MMMM yyyy"));
}

QString itemSubtitle(const ContentItem &item)
{
    static const QRegularExpression label(QStringLiteral(R"(^\s*(Куплет|Приспів|Припев|Verse|Chorus|Bridge)\b)"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const auto firstLine = [](const QString &text, bool skipLabels) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty() && (!skipLabels || !label.match(trimmed).hasMatch()))
                return trimmed;
        }
        return QString();
    };
    switch (item.type) {
    case ContentType::Song: {
        const QString line = firstLine(item.text, true);
        return line.isEmpty() ? QObject::tr("Песня") : line;
    }
    case ContentType::BibleVerse:
    case ContentType::Announcement: {
        const QString line = firstLine(item.text, false);
        return line.isEmpty() ? typeStyle(item.type).label : line;
    }
    case ContentType::Photo:
        return item.caption.isEmpty() ? QObject::tr("Фото") : item.caption;
    case ContentType::Video:
        return isRemote(item.imagePath) ? QObject::tr("Видео по ссылке")
                                        : QObject::tr("Видео · %1").arg(QFileInfo(item.imagePath).suffix().toUpper());
    }
    return QString();
}

void drawCover(QPainter &painter, const QRectF &bounds, const QPixmap &pixmap)
{
    const QSizeF source = pixmap.size();
    const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
    const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
    painter.drawPixmap(bounds, pixmap,
                       QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
}

// A cover image, or a dark tile with the playlist icon when there's none.
void drawCoverBox(QPainter &painter, const QRectF &bounds, const QPixmap &pixmap, qreal radius)
{
    QPainterPath clip;
    clip.addRoundedRect(bounds, Theme::radius(radius), Theme::radius(radius));
    painter.save();
    painter.setClipPath(clip);
    if (!pixmap.isNull()) {
        drawCover(painter, bounds, pixmap);
    } else {
        QLinearGradient fill(bounds.topLeft(), bounds.bottomRight());
        fill.setColorAt(0, QColor(0x2f, 0x3a, 0x52));
        fill.setColorAt(1, QColor(0x1b, 0x23, 0x33));
        painter.fillRect(bounds, fill);
        const int size = qBound(16, int(bounds.height() * 0.3), 40);
        painter.drawPixmap(QPointF(bounds.center().x() - size / 2.0, bounds.center().y() - size / 2.0),
                           IconProvider::pixmap(QStringLiteral("list-music"), QColor(255, 255, 255, 150), size));
    }
    painter.restore();
}

QFont pixelFont(const QFont &base, double size, QFont::Weight weight)
{
    QFont font = base;
    font.setPixelSize(qMax(1, qRound(size)));
    font.setWeight(weight);
    return font;
}

// "icon value" pairs (design.pen "Meta"), left to right; returns the x after.
qreal drawMeta(QPainter &painter, qreal x, qreal centerY, const QString &iconName, const QString &value, int iconSize, double fontSize,
               int gap)
{
    painter.drawPixmap(QPointF(x, centerY - iconSize / 2.0), IconProvider::pixmap(iconName, QColor(Theme::TextDarkSecondary), iconSize));
    x += iconSize + gap;
    painter.setFont(pixelFont(painter.font(), fontSize, QFont::Normal));
    painter.setPen(QColor(Theme::TextDarkSecondary));
    const qreal width = QFontMetricsF(painter.font()).horizontalAdvance(value);
    painter.drawText(QRectF(x, centerY - 10, width + 2, 20), Qt::AlignLeft | Qt::AlignVCenter, value);
    return x + width;
}

// Shared click plumbing for the playlist cards.
class PlaylistItemWidget : public QWidget {
public:
    PlaylistItemWidget(const Playlist &playlist, const QList<PlaylistEntry> &entries)
        : m_playlist(playlist)
        , m_entries(entries)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setSelected(bool selected)
    {
        if (m_selected != selected) {
            m_selected = selected;
            update();
        }
    }

    std::function<void()> onClick;
    std::function<void()> onDoubleClick;
    std::function<void(const QPoint &)> onMenu;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick)
            onClick();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onDoubleClick)
            onDoubleClick();
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onClick)
            onClick();
        if (onMenu)
            onMenu(event->globalPos());
    }

    Playlist m_playlist;
    QList<PlaylistEntry> m_entries;
    bool m_selected = false;
};

// Left column card (design.pen "Playlist …"): padding 8, gap 12, radius 10,
// thumb 92×92; title 13.5/700, description 11.5, meta 11, date 11.
class PlaylistListCard : public PlaylistItemWidget {
public:
    using PlaylistItemWidget::PlaylistItemWidget;

    QSize sizeHint() const override { return QSize(310, 108); }
    QSize minimumSizeHint() const override { return QSize(200, 108); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        painter.setPen(QPen(m_selected ? QColor(Theme::AccentBlue) : QColor(Theme::BorderLight), m_selected ? 1.5 : 1));
        painter.setBrush(m_selected ? QColor(Theme::AccentBlueBg) : (underMouse() ? QColor(Theme::SurfaceMuted) : QColor(Theme::SurfaceSubtle)));
        painter.drawRoundedRect(bounds, Theme::radius(10), Theme::radius(10));

        drawCoverBox(painter, QRectF(8, 8, 92, 92), coverPixmap(m_playlist, m_entries), 8);

        const qreal left = 112;
        const qreal textWidth = width() - left - 10;
        painter.setFont(pixelFont(painter.font(), 13.5, QFont::Bold));
        painter.setPen(QColor(Theme::TextDarkPrimary));
        painter.drawText(QRectF(left, 12, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_playlist.name, Qt::ElideRight, int(textWidth)));

        qreal y = 34;
        if (!m_playlist.description.isEmpty()) {
            painter.setFont(pixelFont(painter.font(), 11.5, QFont::Normal));
            painter.setPen(QColor(Theme::TextDarkSecondary));
            const QFontMetricsF metrics(painter.font());
            const qreal height = qMin(metrics.boundingRect(QRectF(0, 0, textWidth, 1000), Qt::TextWordWrap, m_playlist.description).height(),
                                      metrics.lineSpacing() * 2);
            painter.save();
            painter.setClipRect(QRectF(left, y, textWidth, height));
            painter.drawText(QRectF(left, y, textWidth, height), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_playlist.description);
            painter.restore();
            y += height + 4;
        }
        const qreal metaY = qMax(y + 8, 66.0);
        const qreal after = drawMeta(painter, left, metaY, QStringLiteral("calendar-days"), countText(m_entries.size()), 12, 11, 4);
        const QString total = totalText(m_entries);
        const qreal totalWidth = 12 + 4 + QFontMetricsF(pixelFont(painter.font(), 11, QFont::Normal)).horizontalAdvance(total);
        const bool sameLine = after + 10 + totalWidth <= width() - 10;
        if (sameLine)
            drawMeta(painter, after + 10, metaY, QStringLiteral("clock"), total, 12, 11, 4);
        painter.setFont(pixelFont(painter.font(), 11, QFont::Normal));
        painter.setPen(QColor(Theme::TextDarkSecondary));
        const QString date = dateText(m_playlist.createdAt);
        painter.drawText(QRectF(left, metaY + 10, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, date);
        // Narrow card: the length moves next to the date instead.
        if (!sameLine) {
            const qreal dateWidth = QFontMetricsF(painter.font()).horizontalAdvance(date);
            if (left + dateWidth + 10 + totalWidth <= width() - 10)
                drawMeta(painter, left + dateWidth + 10, metaY + 20, QStringLiteral("clock"), total, 12, 11, 4);
        }
    }
};

// Grid view card: cover on top, name and meta below.
class PlaylistGridCard : public PlaylistItemWidget {
public:
    using PlaylistItemWidget::PlaylistItemWidget;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qRound((width - 16) * 0.6) + 16 + 62; }
    QSize sizeHint() const override { return QSize(240, heightForWidth(240)); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        painter.setPen(QPen(m_selected ? QColor(Theme::AccentBlue) : QColor(Theme::BorderLight), m_selected ? 1.5 : 1));
        painter.setBrush(m_selected ? QColor(Theme::AccentBlueBg) : (underMouse() ? QColor(Theme::SurfaceMuted) : QColor(Theme::SurfaceSubtle)));
        painter.drawRoundedRect(bounds, Theme::radius(10), Theme::radius(10));
        const qreal coverHeight = qRound((width() - 16) * 0.6);
        drawCoverBox(painter, QRectF(8, 8, width() - 16, coverHeight), coverPixmap(m_playlist, m_entries), 8);

        const qreal top = 8 + coverHeight + 10;
        painter.setFont(pixelFont(painter.font(), 14, QFont::Bold));
        painter.setPen(QColor(Theme::TextDarkPrimary));
        painter.drawText(QRectF(12, top, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_playlist.name, Qt::ElideRight, width() - 24));
        const qreal after = drawMeta(painter, 12, top + 32, QStringLiteral("calendar-days"), countText(m_entries.size()), 12, 11, 4);
        drawMeta(painter, after + 10, top + 32, QStringLiteral("clock"), totalText(m_entries), 12, 11, 4);
    }
};

} // namespace

// A description of at most two lines, the second elided.
class TwoLineLabel : public QLabel {
public:
    TwoLineLabel()
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(200, QFontMetrics(font()).lineSpacing() * 2 + 2);
    }
    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        const QFontMetrics metrics(font());
        QString remaining = text().simplified();
        int y = 0;
        for (int line = 0; line < 2 && !remaining.isEmpty(); ++line) {
            QString current;
            if (line == 1 || metrics.horizontalAdvance(remaining) <= width()) {
                current = metrics.elidedText(remaining, Qt::ElideRight, width());
                remaining.clear();
            } else {
                // Break at the last space that fits.
                int cut = remaining.size();
                while (cut > 0 && metrics.horizontalAdvance(remaining.left(cut)) > width())
                    cut = remaining.lastIndexOf(QLatin1Char(' '), cut - 1);
                if (cut <= 0)
                    cut = qMax(1, int(remaining.size()) / 2);
                current = remaining.left(cut);
                remaining = remaining.mid(cut).trimmed();
            }
            painter.drawText(QRect(0, y, width(), metrics.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter, current);
            y += metrics.lineSpacing();
        }
    }
};

// design.pen "Detail Cover": 170×120, radius 8.
class PlaylistCover : public QWidget {
public:
    PlaylistCover() { setFixedSize(170, 120); }

    void setPlaylist(const Playlist &playlist, const QList<PlaylistEntry> &entries)
    {
        m_playlist = playlist;
        m_entries = entries;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        drawCoverBox(painter, QRectF(rect()), coverPixmap(m_playlist, m_entries), 8);
    }

private:
    Playlist m_playlist;
    QList<PlaylistEntry> m_entries;
};

namespace {

enum EntryRole {
    RowIdRole = Qt::UserRole,
    TitleRole,
    SubtitleRole,
    TypeRole,
    DurationRole,
    LiveRole,
    EntryIndexRole,
};

// design.pen "Items Table" columns: handle 18, # 18, element (fill),
// type 104, duration 80, actions 62; padding [0, 10], gap 10. Narrow
// widths drop the type, then the duration column.
struct TableColumns {
    QRectF handle, number, element, type, duration, actions;
    bool showType = true;
    bool showDuration = true;
};

TableColumns columnsFor(const QRectF &row)
{
    TableColumns columns;
    columns.showType = row.width() >= 560;
    columns.showDuration = row.width() >= 440;
    const qreal gap = 10;
    qreal x = row.left() + 10;
    columns.handle = QRectF(x, row.top(), 18, row.height());
    x += 18 + gap;
    columns.number = QRectF(x, row.top(), 18, row.height());
    x += 18 + gap;
    qreal right = row.right() - 10;
    columns.actions = QRectF(right - 62, row.top(), 62, row.height());
    right -= 62 + gap;
    if (columns.showDuration) {
        columns.duration = QRectF(right - 80, row.top(), 80, row.height());
        right -= 80 + gap;
    }
    if (columns.showType) {
        columns.type = QRectF(right - 104, row.top(), 104, row.height());
        right -= 104 + gap;
    }
    columns.element = QRectF(x, row.top(), qMax<qreal>(40, right - x), row.height());
    return columns;
}

QRectF copyRect(const TableColumns &columns)
{
    return QRectF(columns.actions.left(), columns.actions.center().y() - 14, 28, 28);
}

QRectF deleteRect(const TableColumns &columns)
{
    return QRectF(columns.actions.left() + 34, columns.actions.center().y() - 14, 28, 28);
}

class EntryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Thumbnail source for a row, looked up from the panel's entries.
    std::function<ContentItem(int rowId)> itemFor;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(option.rect.width(), 50);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF row = QRectF(option.rect);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool live = index.data(LiveRole).toBool();
        if (selected)
            painter->fillRect(row, QColor(Theme::AccentBlueBg));
        else if (hover)
            painter->fillRect(row, QColor(Theme::SurfaceSubtle));
        painter->fillRect(QRectF(row.left(), row.bottom() - 0.5, row.width(), 1), QColor(Theme::BorderLight));

        const TableColumns columns = columnsFor(row);
        painter->drawPixmap(QPointF(columns.handle.center().x() - 8, columns.handle.center().y() - 8),
                            IconProvider::pixmap(QStringLiteral("grip-vertical"), QColor(Theme::TextDarkSecondary), 16));
        if (live) {
            painter->drawPixmap(QPointF(columns.number.center().x() - 7, columns.number.center().y() - 7),
                                IconProvider::pixmap(QStringLiteral("play"), QColor(Theme::AccentBlue), 14, true));
        } else {
            painter->setFont(pixelFont(option.font, 13, QFont::Medium));
            painter->setPen(QColor(Theme::TextDarkPrimary));
            painter->drawText(columns.number, Qt::AlignLeft | Qt::AlignVCenter, QString::number(index.row() + 1));
        }

        const ContentItem item = itemFor ? itemFor(index.data(RowIdRole).toInt()) : ContentItem();
        const TypeStyle style = typeStyle(ContentType(index.data(TypeRole).toInt()));
        const QRectF thumb(columns.element.left(), row.center().y() - 18, 52, 36);
        QPainterPath clip;
        clip.addRoundedRect(thumb, Theme::radius(5), Theme::radius(5));
        painter->save();
        painter->setClipPath(clip);
        const QPixmap pixmap = itemPixmap(item);
        if (!pixmap.isNull()) {
            drawCover(*painter, thumb, pixmap);
        } else {
            painter->fillRect(thumb, QColor(style.background));
            painter->drawPixmap(QPointF(thumb.center().x() - 9, thumb.center().y() - 9),
                                IconProvider::pixmap(style.icon, QColor(style.foreground), 18));
        }
        painter->restore();

        const qreal textLeft = thumb.right() + 10;
        const qreal textWidth = columns.element.right() - textLeft;
        painter->setFont(pixelFont(option.font, 13.5, QFont::DemiBold));
        painter->setPen(QColor(live ? Theme::AccentBlue : Theme::TextDarkPrimary));
        painter->drawText(QRectF(textLeft, row.top() + 7, textWidth, 19), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(index.data(TitleRole).toString(), Qt::ElideRight, int(textWidth)));
        painter->setFont(pixelFont(option.font, 11, QFont::Normal));
        painter->setPen(QColor(Theme::TextDarkSecondary));
        painter->drawText(QRectF(textLeft, row.top() + 27, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(index.data(SubtitleRole).toString(), Qt::ElideRight, int(textWidth)));

        if (columns.showType) {
            // Badge: 24 tall, radius 12, padding [0, 9], gap 5, 11/600.
            const QFont badgeFont = pixelFont(option.font, 11, QFont::DemiBold);
            const qreal badgeWidth = 9 + 12 + 5 + QFontMetricsF(badgeFont).horizontalAdvance(style.label) + 9;
            const QRectF badge(columns.type.left(), row.center().y() - 12, qMin(badgeWidth, columns.type.width()), 24);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(style.background));
            painter->drawRoundedRect(badge, Theme::radius(12), Theme::radius(12));
            painter->drawPixmap(QPointF(badge.left() + 9, badge.center().y() - 6), IconProvider::pixmap(style.icon, QColor(style.foreground), 12));
            painter->setFont(badgeFont);
            painter->setPen(QColor(style.foreground));
            painter->drawText(QRectF(badge.left() + 26, badge.top(), badge.width() - 30, badge.height()), Qt::AlignLeft | Qt::AlignVCenter,
                              style.label);
        }
        if (columns.showDuration) {
            painter->setFont(pixelFont(option.font, 12.5, QFont::Normal));
            painter->setPen(QColor(Theme::TextDarkSecondary));
            const QString duration = index.data(DurationRole).toString();
            painter->drawText(columns.duration, Qt::AlignLeft | Qt::AlignVCenter, duration.isEmpty() ? QStringLiteral("—") : duration);
        }
        painter->drawPixmap(QPointF(copyRect(columns).center().x() - 7.5, copyRect(columns).center().y() - 7.5),
                            IconProvider::pixmap(QStringLiteral("copy"), QColor(Theme::TextDarkSecondary), 15));
        painter->drawPixmap(QPointF(deleteRect(columns).center().x() - 7.5, deleteRect(columns).center().y() - 7.5),
                            IconProvider::pixmap(QStringLiteral("trash-2"), QColor(Theme::TextDarkSecondary), 15));
        painter->restore();
    }
};

// design.pen "Table Header": 30 tall, #f5f6f8, radius 6, 11/600.
class TableHeader : public QWidget {
public:
    TableHeader() { setFixedHeight(30); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Theme::SurfaceAlt));
        painter.drawRoundedRect(QRectF(rect()), Theme::radius(6), Theme::radius(6));
        // Same geometry as the rows below (minus the list's scrollbar gutter).
        const TableColumns columns = columnsFor(QRectF(0, 0, width() - m_gutter, height()));
        painter.setFont(pixelFont(font(), 11, QFont::DemiBold));
        painter.setPen(QColor(Theme::TextDarkSecondary));
        painter.drawText(columns.number, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("#"));
        painter.drawText(columns.element, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Элемент"));
        if (columns.showType)
            painter.drawText(columns.type, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Тип"));
        if (columns.showDuration)
            painter.drawText(columns.duration.adjusted(0, 0, 40, 0), Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Длительность"));
    }

public:
    int m_gutter = 0;
};

QString outlineStyle(int radius)
{
    return QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: %2px;").arg(Theme::BorderLight).arg(radius);
}

// 44px detail action (design.pen "Detail Actions" buttons) / 40px toolbar button.
ClickFrame *actionButton(const QString &iconName, const QString &iconColor, const QString &label, bool primary, int height)
{
    auto *button = new ClickFrame;
    button->setFixedHeight(height);
    styleFrame(button, QStringLiteral("PlaylistAction"),
               primary ? QStringLiteral("background: %1; border: none; border-radius: 9px;").arg(Theme::AccentBlue) : outlineStyle(9));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(primary ? 18 : 16, 0, primary ? 18 : 16, 0);
    layout->setSpacing(8);
    layout->addWidget(icon(iconName, iconColor, iconName == QStringLiteral("plus") ? 17 : 16));
    auto *labelWidget = text(label, 13.5, primary ? 600 : 500, primary ? Theme::TextLightPrimary : Theme::TextDarkPrimary);
    labelWidget->setObjectName(QStringLiteral("ButtonLabel"));
    layout->addWidget(labelWidget);
    return button;
}

} // namespace

// ---------------------------------------------------------------------------

PlaylistsPanel::PlaylistsPanel(ContentRepository *contentRepository, PlaylistRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_contentRepository(contentRepository)
    , m_repository(repository)
{
    setObjectName(QStringLiteral("PlaylistsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#PlaylistsPanel { background: #ffffff; }"));
    m_gridView = QSettings().value(QStringLiteral("playlists/gridView"), false).toBool();

    // design.pen "Playlists Content": vertical, gap 18, padding [22, 24, 16, 24].
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 16);
    layout->setSpacing(18);
    layout->addWidget(text(tr("Плейлисты"), 30, 800, Theme::TextDarkPrimary));
    layout->addWidget(buildToolbar());

    m_viewStack = new QStackedWidget;

    // ---- List view: "Body Row" (gap 14) = playlist list + detail card ----
    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(14);
    m_listScroll = new QScrollArea;
    m_listScroll->setFrameShape(QFrame::NoFrame);
    m_listScroll->setWidgetResizable(true);
    m_listScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listScroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    m_listScroll->setMinimumWidth(240);
    m_listScroll->setMaximumWidth(310);
    m_listWidget = new QWidget;
    m_listWidget->setObjectName(QStringLiteral("PlaylistListItems"));
    m_listWidget->setStyleSheet(QStringLiteral("QWidget#PlaylistListItems { background: #ffffff; }"));
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(10);
    m_listLayout->setAlignment(Qt::AlignTop);
    m_listScroll->setWidget(m_listWidget);
    bodyLayout->addWidget(m_listScroll, 1);
    bodyLayout->addWidget(buildDetail(), 3);
    m_viewStack->addWidget(body);

    // ---- Grid view ----
    m_gridScroll = new QScrollArea;
    m_gridScroll->setFrameShape(QFrame::NoFrame);
    m_gridScroll->setWidgetResizable(true);
    m_gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridScroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    m_gridWidget = new QWidget;
    m_gridWidget->setObjectName(QStringLiteral("PlaylistGridItems"));
    m_gridWidget->setStyleSheet(QStringLiteral("QWidget#PlaylistGridItems { background: #ffffff; }"));
    auto *flow = new FlowLayout(FlowLayout::Mode::EqualColumns, 14, 200, m_gridWidget);
    flow->setContentsMargins(0, 0, 0, 8);
    flow->setColumnsFromWidthOnly(true);
    m_gridScroll->setWidget(m_gridWidget);
    m_viewStack->addWidget(m_gridScroll);
    layout->addWidget(m_viewStack, 1);

    const auto repaintAll = [this]() {
        for (QWidget *card : std::as_const(m_listCards))
            card->update();
        for (QWidget *card : m_gridWidget->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            card->update();
        m_cover->update();
        m_table->viewport()->update();
    };
    connect(ThumbnailCache::instance(), &ThumbnailCache::ready, this, repaintAll);
    connect(VideoThumbnailer::instance(), &VideoThumbnailer::ready, this, [this, repaintAll]() {
        repaintAll();
        // Video lengths arrive with the posters: refresh durations/totals.
        if (const Playlist *current = playlist(m_selectedId)) {
            for (int i = 0; i < m_table->count(); ++i) {
                QListWidgetItem *item = m_table->item(i);
                const int rowId = item->data(RowIdRole).toInt();
                for (const PlaylistEntry &entry : m_entries.value(current->id)) {
                    if (entry.rowId == rowId) {
                        const int seconds = entrySeconds(entry);
                        item->setData(DurationRole, seconds > 0 ? formatClock(seconds) : QString());
                    }
                }
            }
            m_detailDuration->setText(totalText(m_entries.value(current->id)));
        }
    });

    setGridView(m_gridView);
}

QWidget *PlaylistsPanel::buildToolbar()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // design.pen "Toolbar Row": search 310 × 40, then (space) add, toggle, sort.
    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    m_searchEdit->setPlaceholderText(tr("Поиск плейлистов…"));
    m_searchEdit->setFixedHeight(40);
    m_searchEdit->setMinimumWidth(140);
    m_searchEdit->setMaximumWidth(310);
    m_searchEdit->addAction(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16),
                            QLineEdit::LeadingPosition);
    m_searchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; border: 1px solid %1; border-radius: 9px; padding: 0 10px; font-size: 13.5px; color: %2; }"
        "QLineEdit:focus { border: 1px solid %3; }").arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &value) {
        m_search = value.trimmed();
        rebuildList();
        rebuildGrid();
        showSelected();
    });
    layout->addWidget(m_searchEdit, 1);
    layout->addStretch();

    ClickFrame *add = actionButton(QStringLiteral("plus"), Theme::TextLightPrimary, tr("Добавить плейлист"), true, 40);
    add->setContentsMargins(0, 0, 0, 0);
    add->setToolTip(tr("Добавить плейлист"));
    add->onClick = [this]() { createPlaylist(); };
    struct LabelCollapser : QObject {
        QWidget *row;
        QWidget *label;
        LabelCollapser(QWidget *r, QWidget *l) : QObject(r), row(r), label(l) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                label->setVisible(row->width() >= 640);
            return false;
        }
    };
    row->installEventFilter(new LabelCollapser(row, add->findChild<QLabel *>(QStringLiteral("ButtonLabel"))));
    layout->addWidget(add);

    // design.pen "View Toggle": list | grid, 40×40 segments.
    auto *toggle = new QFrame;
    toggle->setFixedHeight(40);
    styleFrame(toggle, QStringLiteral("PlaylistViewToggle"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
    auto *toggleLayout = new QHBoxLayout(toggle);
    toggleLayout->setContentsMargins(1, 1, 1, 1);
    toggleLayout->setSpacing(0);
    m_listButton = new ClickFrame;
    m_gridButton = new ClickFrame;
    m_listButton->setToolTip(tr("Список"));
    m_gridButton->setToolTip(tr("Плитка"));
    for (ClickFrame *button : {m_listButton, m_gridButton}) {
        button->setFixedSize(39, 38);
        auto *buttonLayout = new QHBoxLayout(button);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        auto *iconLabel = new QLabel;
        iconLabel->setObjectName(QStringLiteral("ToggleIcon"));
        iconLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        buttonLayout->addWidget(iconLabel, 0, Qt::AlignCenter);
    }
    m_listButton->onClick = [this]() { setGridView(false); };
    m_gridButton->onClick = [this]() { setGridView(true); };
    toggleLayout->addWidget(m_listButton);
    toggleLayout->addWidget(m_gridButton);
    layout->addWidget(toggle);

    m_sortField = new DropdownField(true);
    m_sortField->setOptions({tr("По дате (сначала новые)"), tr("По дате (сначала старые)"), tr("По названию")});
    m_sortField->setCurrent(0);
    m_sortField->setFixedHeight(40);
    m_sortField->setMinimumWidth(150);
    m_sortField->onSelected = [this](int index) {
        m_sort = index;
        rebuildList();
        rebuildGrid();
    };
    layout->addWidget(m_sortField);
    return row;
}

QWidget *PlaylistsPanel::buildDetail()
{
    m_detailStack = new QStackedWidget;

    // design.pen "Playlist Detail Card": radius 12, padding 16, gap 14.
    auto *card = new QFrame;
    m_detailCard = card;
    card->installEventFilter(this);
    card->setObjectName(QStringLiteral("PlaylistDetailCard"));
    card->setStyleSheet(QStringLiteral("QFrame#PlaylistDetailCard { background: #ffffff; border: 1px solid %1; border-radius: 12px; }").arg(Theme::BorderLight));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    // "Detail Header": cover, text column, "⋯".
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    m_cover = new PlaylistCover;
    m_cover->setCursor(Qt::PointingHandCursor);
    m_cover->setToolTip(tr("Изменить обложку"));
    m_cover->installEventFilter(this);
    header->addWidget(m_cover, 0, Qt::AlignTop);
    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(8);
    m_detailTitle = new ElideLabel;
    m_detailTitle->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: 19px; font-weight: 800;")
                                     .arg(Theme::TextDarkPrimary));
    textColumn->addWidget(m_detailTitle);
    m_detailDescription = new TwoLineLabel;
    m_detailDescription->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: 12.5px;")
                                           .arg(Theme::TextDarkSecondary));
    m_detailDescription->setCursor(Qt::PointingHandCursor);
    m_detailDescription->setToolTip(tr("Изменить описание"));
    m_detailDescription->installEventFilter(this);
    textColumn->addWidget(m_detailDescription);
    auto *meta = new QHBoxLayout;
    meta->setSpacing(22);
    const auto metaItem = [meta](const QString &iconName) {
        auto *box = new QWidget;
        auto *itemLayout = new QHBoxLayout(box);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(7);
        itemLayout->addWidget(icon(iconName, Theme::TextDarkSecondary, 15));
        QLabel *value = text(QString(), 12.5, 400, Theme::TextDarkSecondary);
        itemLayout->addWidget(value);
        meta->addWidget(box);
        return value;
    };
    m_detailDate = metaItem(QStringLiteral("calendar-days"));
    m_detailDateBox = m_detailDate->parentWidget();
    m_detailCount = metaItem(QStringLiteral("list"));
    m_detailDuration = metaItem(QStringLiteral("clock"));
    meta->addStretch();
    textColumn->addLayout(meta);
    textColumn->addStretch();
    header->addLayout(textColumn, 1);
    auto *more = new ClickFrame;
    more->setFixedSize(34, 34);
    more->setToolTip(tr("Ещё"));
    styleFrame(more, QStringLiteral("PlaylistMore"), outlineStyle(8));
    auto *moreLayout = new QHBoxLayout(more);
    moreLayout->setContentsMargins(0, 0, 0, 0);
    moreLayout->addWidget(icon(QStringLiteral("ellipsis"), Theme::TextDarkPrimary, 16), 0, Qt::AlignCenter);
    more->onClick = [this, more]() {
        if (m_selectedId >= 0)
            showPlaylistMenu(m_selectedId, more->mapToGlobal(QPoint(0, more->height() + 4)));
    };
    header->addWidget(more, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto *divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(Theme::BorderLight));
    layout->addWidget(divider);

    layout->addWidget(text(tr("Элементы плейлиста"), 16, 700, Theme::TextDarkPrimary));

    // "Items Table".
    auto *table = new QVBoxLayout;
    table->setSpacing(0);
    auto *tableHeader = new TableHeader;
    m_tableHeader = tableHeader;
    table->addWidget(tableHeader);
    m_table = new QListWidget;
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setDragDropMode(QAbstractItemView::InternalMove);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setUniformItemSizes(true);
    m_table->setMouseTracking(true);
    m_table->setStyleSheet(QStringLiteral("QListWidget { background: #ffffff; border: none; outline: none; }") + Theme::scrollBarCss());
    auto *delegate = new EntryDelegate(m_table);
    delegate->itemFor = [this](int rowId) {
        for (const PlaylistEntry &entry : m_entries.value(m_selectedId)) {
            if (entry.rowId == rowId)
                return entry.item;
        }
        return ContentItem();
    };
    m_table->setItemDelegate(delegate);
    m_table->viewport()->installEventFilter(this);
    connect(m_table, &QListWidget::currentRowChanged, this, [this](int row) {
        if (QListWidgetItem *item = m_table->item(row))
            m_selectedRowId = item->data(RowIdRole).toInt();
    });
    connect(m_table->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        // Deferred: the drop is still being processed.
        QTimer::singleShot(0, this, [this]() {
            if (m_selectedId < 0)
                return;
            QList<int> order;
            for (int i = 0; i < m_table->count(); ++i)
                order << m_table->item(i)->data(RowIdRole).toInt();
            m_repository->reorder(m_selectedId, order);
            m_entries.insert(m_selectedId, m_repository->entries(m_selectedId));
            m_table->viewport()->update();
            if (QWidget *card = m_listCards.value(m_selectedId))
                card->update();
            m_cover->setPlaylist(*playlist(m_selectedId), m_entries.value(m_selectedId));
        });
    });
    // The header's columns must line up with the rows, which lose the
    // scrollbar's width when it shows.
    struct GutterWatcher : QObject {
        QListWidget *list;
        TableHeader *header;
        GutterWatcher(QListWidget *l, TableHeader *h) : QObject(l), list(l), header(h) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide) {
                header->m_gutter = list->width() - list->viewport()->width();
                header->update();
            }
            return false;
        }
    };
    auto *watcher = new GutterWatcher(m_table, tableHeader);
    m_table->viewport()->installEventFilter(watcher);
    m_table->verticalScrollBar()->installEventFilter(watcher);
    table->addWidget(m_table, 1);
    m_tableEmpty = text(tr("В плейлисте пока нет элементов — нажмите «Добавить элемент»."), 13, 500, Theme::TextDarkSecondary);
    m_tableEmpty->setAlignment(Qt::AlignCenter);
    m_tableEmpty->setWordWrap(true);
    m_tableEmpty->setStyleSheet(m_tableEmpty->styleSheet() + QStringLiteral(" border: none; padding: 24px;"));
    table->addWidget(m_tableEmpty);
    layout->addLayout(table, 1);

    // "Detail Actions": add element … preview, run.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    ClickFrame *addItem = actionButton(QStringLiteral("plus"), Theme::TextDarkPrimary, tr("Добавить элемент"), false, 44);
    addItem->setToolTip(tr("Добавить элемент"));
    addItem->onClick = [this]() { addItems(); };
    actions->addWidget(addItem);
    actions->addStretch();
    ClickFrame *preview = actionButton(QStringLiteral("play"), Theme::AccentBlue, tr("Предпросмотр"), false, 44);
    preview->setToolTip(tr("Предпросмотр выбранного элемента"));
    preview->onClick = [this]() {
        if (const auto item = selectedItem())
            emit previewRequested(*item);
    };
    actions->addWidget(preview);
    ClickFrame *run = actionButton(QStringLiteral("play"), Theme::TextLightPrimary, tr("Запустить плейлист"), true, 44);
    run->setToolTip(tr("Показать плейлист на экране с выбранного элемента (F5).\nВперёд/Назад переходят между элементами."));
    run->onClick = [this]() { goLiveSelected(); };
    actions->addWidget(run);
    layout->addLayout(actions);
    for (ClickFrame *button : {addItem, preview, run}) {
        QLabel *label = button->findChild<QLabel *>(QStringLiteral("ButtonLabel"));
        m_actionLabels.append({label, label->text()});
    }
    m_detailStack->addWidget(card);

    // No playlist selected / none yet.
    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(12);
    auto *emptyIcon = new QLabel;
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(IconProvider::pixmap(QStringLiteral("list-music"), QColor(Theme::Placeholder), 44));
    emptyLayout->addWidget(emptyIcon);
    m_detailEmpty = text(QString(), 14, 500, Theme::TextDarkSecondary);
    m_detailEmpty->setAlignment(Qt::AlignCenter);
    m_detailEmpty->setWordWrap(true);
    emptyLayout->addWidget(m_detailEmpty);
    m_detailStack->addWidget(empty);
    return m_detailStack;
}

void PlaylistsPanel::setGridView(bool grid)
{
    m_gridView = grid;
    QSettings().setValue(QStringLiteral("playlists/gridView"), grid);
    const auto paint = [](ClickFrame *button, bool on, const QString &iconName, bool leftEdge) {
        button->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; border-top-%2-radius: 8px; border-bottom-%2-radius: 8px; }")
                                  .arg(on ? Theme::AccentBlueBg : QStringLiteral("#ffffff"),
                                       leftEdge ? QStringLiteral("left") : QStringLiteral("right")));
        button->findChild<QLabel *>(QStringLiteral("ToggleIcon"))
            ->setPixmap(IconProvider::pixmap(iconName, QColor(on ? Theme::AccentBlue : Theme::TextDarkSecondary), 17));
    };
    paint(m_listButton, !grid, QStringLiteral("list"), true);
    paint(m_gridButton, grid, QStringLiteral("layout-grid"), false);
    m_viewStack->setCurrentIndex(grid ? 1 : 0);
}

// ---- Data ------------------------------------------------------------------

void PlaylistsPanel::reload()
{
    m_playlists = m_repository->fetch(QString(), SortOrder::DateAddedDesc);
    m_entries.clear();
    for (const Playlist &item : std::as_const(m_playlists))
        m_entries.insert(item.id, m_repository->entries(item.id));
    emit countChanged(m_playlists.size());
    rebuildList();
    rebuildGrid();
    showSelected();
}

const Playlist *PlaylistsPanel::playlist(int id) const
{
    for (const Playlist &item : m_playlists) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

QList<Playlist> PlaylistsPanel::visiblePlaylists() const
{
    QList<Playlist> result;
    for (const Playlist &item : m_playlists) {
        if (m_search.isEmpty() || item.name.contains(m_search, Qt::CaseInsensitive)
            || item.description.contains(m_search, Qt::CaseInsensitive))
            result << item;
    }
    std::stable_sort(result.begin(), result.end(), [this](const Playlist &a, const Playlist &b) {
        switch (m_sort) {
        case 1: return a.createdAt < b.createdAt;
        case 2: return QString::localeAwareCompare(a.name, b.name) < 0;
        default: return a.createdAt > b.createdAt;
        }
    });
    return result;
}

void PlaylistsPanel::rebuildList()
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_listCards.clear();

    const QList<Playlist> shown = visiblePlaylists();
    if (!shown.isEmpty() && !playlist(m_selectedId))
        m_selectedId = shown.first().id;
    for (const Playlist &item : shown) {
        auto *card = new PlaylistListCard(item, m_entries.value(item.id));
        const int id = item.id;
        // Deferred: selecting rebuilds parts of the screen.
        card->onClick = [this, id]() { QTimer::singleShot(0, this, [this, id]() { selectPlaylist(id); }); };
        card->onDoubleClick = [this, id]() {
            QTimer::singleShot(0, this, [this, id]() {
                selectPlaylist(id);
                startPlaylist(0);
            });
        };
        card->onMenu = [this, id](const QPoint &pos) { QTimer::singleShot(0, this, [this, id, pos]() { showPlaylistMenu(id, pos); }); };
        card->setSelected(id == m_selectedId);
        m_listLayout->addWidget(card);
        m_listCards.insert(id, card);
    }
    if (shown.isEmpty()) {
        QLabel *hint = text(m_playlists.isEmpty() ? tr("Плейлистов пока нет.\nНажмите «Добавить плейлист».") : tr("Ничего не найдено"),
                            13, 500, Theme::TextDarkSecondary);
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        hint->setContentsMargins(0, 24, 0, 0);
        m_listLayout->addWidget(hint);
    }
}

void PlaylistsPanel::rebuildGrid()
{
    QLayout *layout = m_gridWidget->layout();
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const Playlist &item : visiblePlaylists()) {
        auto *card = new PlaylistGridCard(item, m_entries.value(item.id));
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        card->setSizePolicy(policy);
        const int id = item.id;
        // A grid card opens the playlist in the list view.
        card->onClick = [this, id]() {
            QTimer::singleShot(0, this, [this, id]() {
                selectPlaylist(id);
                setGridView(false);
            });
        };
        card->onMenu = [this, id](const QPoint &pos) { QTimer::singleShot(0, this, [this, id, pos]() { showPlaylistMenu(id, pos); }); };
        card->setSelected(id == m_selectedId);
        layout->addWidget(card);
    }
}

void PlaylistsPanel::selectPlaylist(int id)
{
    if (!playlist(id))
        return;
    if (id != m_selectedId)
        m_selectedRowId = -1;
    m_selectedId = id;
    for (auto it = m_listCards.constBegin(); it != m_listCards.constEnd(); ++it)
        static_cast<PlaylistItemWidget *>(it.value())->setSelected(it.key() == id);
    for (QWidget *card : m_gridWidget->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
        if (auto *gridCard = dynamic_cast<PlaylistItemWidget *>(card))
            gridCard->update();
    rebuildGrid();
    showSelected();
}

void PlaylistsPanel::showSelected()
{
    const Playlist *current = playlist(m_selectedId);
    if (!current) {
        m_detailEmpty->setText(m_playlists.isEmpty() ? tr("Создайте первый плейлист — порядок песен, стихов, объявлений и видео для служения.")
                                                     : tr("Выберите плейлист слева"));
        m_detailStack->setCurrentIndex(1);
        return;
    }
    m_detailStack->setCurrentIndex(0);
    const QList<PlaylistEntry> list = m_entries.value(current->id);
    m_cover->setPlaylist(*current, list);
    m_detailTitle->setText(current->name);
    m_detailTitle->setToolTip(current->name);
    m_detailDescription->setText(current->description.isEmpty() ? tr("Добавить описание…") : current->description);
    m_detailDate->setText(dateText(current->createdAt));
    m_detailCount->setText(countText(list.size()));
    m_detailDuration->setText(totalText(list));
    rebuildTable();
}

void PlaylistsPanel::rebuildTable()
{
    const QList<PlaylistEntry> list = m_entries.value(m_selectedId);
    const QSignalBlocker blocker(m_table);
    m_table->clear();
    int selectedRow = 0;
    for (int i = 0; i < list.size(); ++i) {
        const PlaylistEntry &entry = list.at(i);
        auto *item = new QListWidgetItem;
        item->setData(RowIdRole, entry.rowId);
        item->setData(TitleRole, entry.item.displayTitle());
        item->setData(SubtitleRole, itemSubtitle(entry.item));
        item->setData(TypeRole, int(entry.item.type));
        const int seconds = entrySeconds(entry);
        item->setData(DurationRole, seconds > 0 ? formatClock(seconds) : QString());
        item->setData(LiveRole, m_livePlaylistId == m_selectedId && m_liveIndex == i);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        m_table->addItem(item);
        if (entry.rowId == m_selectedRowId)
            selectedRow = i;
    }
    if (!list.isEmpty()) {
        m_table->setCurrentRow(selectedRow);
        m_selectedRowId = list.at(selectedRow).rowId;
    }
    m_table->setVisible(!list.isEmpty());
    m_tableEmpty->setVisible(list.isEmpty());
}

int PlaylistsPanel::selectedRowIndex() const
{
    const QList<PlaylistEntry> list = m_entries.value(m_selectedId);
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).rowId == m_selectedRowId)
            return i;
    }
    return list.isEmpty() ? -1 : 0;
}

std::optional<ContentItem> PlaylistsPanel::selectedItem() const
{
    const int index = selectedRowIndex();
    if (index < 0)
        return std::nullopt;
    return m_entries.value(m_selectedId).at(index).item;
}

void PlaylistsPanel::setLiveEntry(int playlistId, int index)
{
    m_livePlaylistId = playlistId;
    m_liveIndex = index;
    if (m_selectedId != playlistId && m_table->count() == 0)
        return;
    for (int i = 0; i < m_table->count(); ++i)
        m_table->item(i)->setData(LiveRole, playlistId == m_selectedId && i == index);
}

void PlaylistsPanel::startPlaylist(int fromIndex)
{
    QList<ContentItem> items;
    for (const PlaylistEntry &entry : m_entries.value(m_selectedId))
        items << entry.item;
    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Плейлист"), tr("В плейлисте нет элементов."));
        return;
    }
    emit goLivePlaylistRequested(items, qBound(0, fromIndex, int(items.size()) - 1), m_selectedId);
}

void PlaylistsPanel::goLiveSelected()
{
    startPlaylist(qMax(0, selectedRowIndex()));
}

// ---- Events ------------------------------------------------------------------

void PlaylistsPanel::layoutDetail(int width)
{
    if (m_actionLabels.size() < 3)
        return;
    // design.pen's 170×120 cover needs a wide card; narrower ones get a
    // smaller cover, then none.
    m_cover->setVisible(width >= 400);
    m_detailDateBox->setVisible(width >= 440);
    if (width >= 600)
        m_cover->setFixedSize(170, 120);
    else
        m_cover->setFixedSize(112, 79);
    // Action labels: all at full width; "Добавить элемент" and
    // "Предпросмотр" drop to icons first, "Запустить плейлист" shortens.
    const int inner = width - 32;
    const bool full = inner >= 540;
    m_actionLabels.at(0).first->setVisible(full);
    m_actionLabels.at(1).first->setVisible(full);
    m_actionLabels.at(2).first->setText(inner >= 300 ? m_actionLabels.at(2).second : tr("Запустить"));
}

bool PlaylistsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_detailCard && event->type() == QEvent::Resize) {
        layoutDetail(m_detailCard->width());
        return false;
    }
    if (watched == m_cover && event->type() == QEvent::MouseButtonRelease) {
        if (m_selectedId >= 0)
            chooseCover(m_selectedId);
        return true;
    }
    if (watched == m_detailDescription && event->type() == QEvent::MouseButtonRelease) {
        if (m_selectedId >= 0)
            editDescription(m_selectedId);
        return true;
    }
    if (!m_table || watched != m_table->viewport())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_table->itemAt(mouse->position().toPoint());
        if (!item || mouse->button() != Qt::LeftButton)
            return false;
        const int rowId = item->data(RowIdRole).toInt();
        const TableColumns columns = columnsFor(QRectF(m_table->visualItemRect(item)));
        const QPointF pos = mouse->position();
        if (copyRect(columns).contains(pos)) {
            if (event->type() == QEvent::MouseButtonPress) {
                QTimer::singleShot(0, this, [this, rowId]() {
                    m_repository->duplicateEntry(rowId);
                    reload();
                });
            }
            return true;
        }
        if (deleteRect(columns).contains(pos)) {
            if (event->type() == QEvent::MouseButtonPress) {
                QTimer::singleShot(0, this, [this, rowId]() {
                    m_repository->removeEntry(rowId);
                    reload();
                });
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (columns.showDuration && columns.duration.contains(pos)) {
                QTimer::singleShot(0, this, [this, rowId]() { editDuration(rowId); });
            } else {
                const int row = m_table->row(item);
                QTimer::singleShot(0, this, [this, row]() { startPlaylist(row); });
            }
            return true;
        }
        return false;
    }
    if (event->type() == QEvent::ContextMenu) {
        auto *menuEvent = static_cast<QContextMenuEvent *>(event);
        if (QListWidgetItem *item = m_table->itemAt(menuEvent->pos())) {
            m_table->setCurrentItem(item);
            showRowMenu(item->data(RowIdRole).toInt(), menuEvent->globalPos());
            return true;
        }
    }
    return false;
}

// ---- Actions -----------------------------------------------------------------

void PlaylistsPanel::createPlaylist()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Новый плейлист"), tr("Название:"), QLineEdit::Normal, tr("Новый плейлист"), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    Playlist created;
    created.name = name;
    if (!m_repository->add(created)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось создать плейлист."));
        return;
    }
    m_selectedId = created.id;
    m_selectedRowId = -1;
    m_search.clear();
    m_searchEdit->clear();
    setGridView(false);
    reload();
}

void PlaylistsPanel::showPlaylistMenu(int id, const QPoint &globalPos)
{
    const Playlist *current = playlist(id);
    if (!current)
        return;
    selectPlaylist(id);
    QMenu menu(this);
    const QColor dark(Theme::TextDarkPrimary);
    menu.addAction(IconProvider::icon(QStringLiteral("play"), dark, 16), tr("Запустить плейлист"), this, [this]() { startPlaylist(0); });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("pencil"), dark, 16), tr("Переименовать…"), this, [this, id]() { renamePlaylist(id); });
    menu.addAction(IconProvider::icon(QStringLiteral("file-text"), dark, 16), tr("Изменить описание…"), this,
                   [this, id]() { editDescription(id); });
    menu.addAction(IconProvider::icon(QStringLiteral("image"), dark, 16), tr("Изменить обложку…"), this, [this, id]() { chooseCover(id); });
    if (!current->coverPath.isEmpty()) {
        menu.addAction(IconProvider::icon(QStringLiteral("x"), dark, 16), tr("Сбросить обложку"), this, [this, id]() {
            m_repository->setCover(id, QString());
            reload();
        });
    }
    menu.addAction(IconProvider::icon(QStringLiteral("copy"), dark, 16), tr("Дублировать"), this, [this, id]() {
        const Playlist *source = playlist(id);
        if (!source)
            return;
        m_repository->duplicate(id, tr("%1 (копия)").arg(source->name));
        reload();
    });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 16), tr("Удалить плейлист"), this,
                   [this, id]() { deletePlaylist(id); });
    menu.exec(globalPos);
}

void PlaylistsPanel::showRowMenu(int rowId, const QPoint &globalPos)
{
    const QList<PlaylistEntry> list = m_entries.value(m_selectedId);
    int index = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).rowId == rowId)
            index = i;
    }
    if (index < 0)
        return;
    const ContentItem item = list.at(index).item;
    const QColor dark(Theme::TextDarkPrimary);
    QMenu menu(this);
    menu.addAction(IconProvider::icon(QStringLiteral("monitor"), dark, 16), tr("Показать с этого элемента"), this,
                   [this, index]() { startPlaylist(index); });
    menu.addAction(IconProvider::icon(QStringLiteral("eye"), dark, 16), tr("Предпросмотр"), this, [this, item]() { emit previewRequested(item); });
    menu.addAction(IconProvider::icon(QStringLiteral("clock"), dark, 16), tr("Указать длительность…"), this,
                   [this, rowId]() { editDuration(rowId); });
    menu.addAction(IconProvider::icon(QStringLiteral("copy"), dark, 16), tr("Повторить"), this, [this, rowId]() {
        m_repository->duplicateEntry(rowId);
        reload();
    });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 16), tr("Убрать из плейлиста"), this,
                   [this, rowId]() {
                       m_repository->removeEntry(rowId);
                       reload();
                   });
    menu.exec(globalPos);
}

void PlaylistsPanel::renamePlaylist(int id)
{
    const Playlist *current = playlist(id);
    if (!current)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Переименовать плейлист"), tr("Название:"), QLineEdit::Normal, current->name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == current->name)
        return;
    m_repository->rename(id, name);
    reload();
}

void PlaylistsPanel::editDescription(int id)
{
    const Playlist *current = playlist(id);
    if (!current)
        return;
    bool ok = false;
    const QString description = QInputDialog::getMultiLineText(this, tr("Описание плейлиста"), tr("Описание:"), current->description, &ok)
                                    .trimmed();
    if (!ok || description == current->description)
        return;
    m_repository->setDescription(id, description);
    reload();
}

void PlaylistsPanel::chooseCover(int id)
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Обложка плейлиста"), QString(),
                                                      tr("Изображения (*.jpg *.jpeg *.png *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    // Copied into Sermon's own folder, like slide backgrounds.
    const QString dest = Database::backgroundsDir() + QStringLiteral("/cover-") + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QLatin1Char('.') + QFileInfo(path).suffix();
    if (!QFile::copy(path, dest)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить обложку."));
        return;
    }
    m_repository->setCover(id, dest);
    reload();
}

void PlaylistsPanel::deletePlaylist(int id)
{
    const Playlist *current = playlist(id);
    if (!current)
        return;
    if (QMessageBox::question(this, tr("Удаление"), tr("Удалить плейлист «%1»?\nСами песни, фото и видео останутся в библиотеке.")
                                                        .arg(current->name))
        != QMessageBox::Yes)
        return;
    m_repository->remove(id);
    if (m_selectedId == id)
        m_selectedId = -1;
    reload();
}

void PlaylistsPanel::editDuration(int rowId)
{
    int current = 0;
    for (const PlaylistEntry &entry : m_entries.value(m_selectedId)) {
        if (entry.rowId == rowId)
            current = entry.durationSec;
    }
    bool ok = false;
    const QString value = QInputDialog::getText(this, tr("Длительность"),
                                                tr("Сколько времени займёт элемент (мм:сс или минуты).\nПусто — не указано."),
                                                QLineEdit::Normal, current > 0 ? formatClock(current) : QString(), &ok)
                              .trimmed();
    if (!ok)
        return;
    int seconds = 0;
    if (!value.isEmpty()) {
        static const QRegularExpression clock(QStringLiteral(R"(^(\d{1,3})(?::(\d{1,2}))?$)"));
        const QRegularExpressionMatch match = clock.match(value);
        if (!match.hasMatch()) {
            QMessageBox::information(this, tr("Длительность"), tr("Введите время как 4:30 или просто число минут."));
            return;
        }
        seconds = match.captured(1).toInt() * 60 + match.captured(2).toInt();
    }
    m_repository->setEntryDuration(rowId, seconds);
    reload();
}

void PlaylistsPanel::addItems()
{
    if (m_selectedId < 0)
        return;
    const QList<ContentItem> library = m_contentRepository->fetch(QString(), std::nullopt, SortOrder::Alphabetical, true);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Добавить в плейлист"));
    dialog.resize(480, 560);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);
    auto *search = new QLineEdit;
    search->setPlaceholderText(tr("Поиск: песня, стих, объявление, фото, видео…"));
    search->setFixedHeight(36);
    search->addAction(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16), QLineEdit::LeadingPosition);
    search->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; border: 1px solid %1; border-radius: 8px; padding: 0 8px; font-size: 13.5px; color: %2; }"
        "QLineEdit:focus { border: 1px solid %3; }").arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue));
    layout->addWidget(search);
    auto *list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setIconSize(QSize(16, 16));
    list->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid %1; border-radius: 8px; font-size: 13px; }"
                                       "QListWidget::item { padding: 6px 4px; }").arg(Theme::BorderLight) + Theme::scrollBarCss());
    for (const ContentItem &item : library) {
        const TypeStyle style = typeStyle(item.type);
        auto *row = new QListWidgetItem(IconProvider::icon(style.icon, QColor(style.foreground), 16),
                                        QStringLiteral("%1   ·   %2").arg(item.displayTitle(), style.label));
        row->setData(Qt::UserRole, item.id);
        list->addItem(row);
    }
    layout->addWidget(list, 1);
    auto *hint = text(tr("Ctrl/Shift — выбрать несколько. Двойной щелчок — добавить один."), 11.5, 400, Theme::TextDarkSecondary);
    layout->addWidget(hint);
    connect(search, &QLineEdit::textChanged, list, [list](const QString &value) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!value.trimmed().isEmpty() && !list->item(i)->text().contains(value.trimmed(), Qt::CaseInsensitive));
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [list, &dialog](QListWidgetItem *item) {
        list->clearSelection();
        item->setSelected(true);
        dialog.accept();
    });
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(tr("Отмена"));
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto *ok = new QPushButton(tr("Добавить"));
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    search->setFocus();

    if (dialog.exec() != QDialog::Accepted)
        return;
    int added = 0;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->isSelected() && !list->item(i)->isHidden()) {
            m_repository->appendItem(m_selectedId, list->item(i)->data(Qt::UserRole).toInt());
            ++added;
        }
    }
    if (added > 0)
        reload();
}
