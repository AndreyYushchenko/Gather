#include "AnnouncementsPanel.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "TimerWidgets.h"
#include "VideoThumbnailer.h"
#include "core/AppSettings.h"
#include "core/ContentRepository.h"
#include "core/Database.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace TimerUi;

namespace {

constexpr double CardRatio = 187.0 / 268.0; // design.pen "Ann Молитвенное собрание"

struct Category {
    QString name;
    QString color;
};

// The "Категория" dropdown's choices, each with its dot colour.
const QList<Category> &categories()
{
    static const QList<Category> list = {
        {QObject::tr("Служения"), Theme::AccentBlue},
        {QObject::tr("События"), QStringLiteral("#16a34a")},
        {QObject::tr("Молодёжь"), QStringLiteral("#7c3aed")},
        {QObject::tr("Молитва"), QStringLiteral("#ea7a0c")},
        {QObject::tr("Общее"), QStringLiteral("#64748b")},
    };
    return list;
}

QString categoryColor(const QString &name)
{
    for (const Category &category : categories()) {
        if (category.name == name)
            return category.color;
    }
    return categories().last().color;
}

QStringList videoExtensions()
{
    return {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"),
            QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("wmv")};
}

bool hasBackground(const ContentItem &item)
{
    return item.backgroundType != BackgroundType::None && !item.backgroundPath.isEmpty();
}

QPixmap backgroundPixmap(const ContentItem &item)
{
    if (!hasBackground(item))
        return QPixmap();
    if (item.backgroundType == BackgroundType::Video)
        return VideoThumbnailer::instance()->poster(item.backgroundPath);
    return ThumbnailCache::instance()->thumbnail(item.backgroundPath);
}

// `text` holds the subtitle on its first line and the extra text after it.
QString subtitleOf(const ContentItem &item)
{
    return item.text.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString extraOf(const ContentItem &item)
{
    QStringList rest = item.text.split(QLatin1Char('\n')).mid(1);
    for (QString &line : rest)
        line = line.trimmed();
    rest.removeAll(QString());
    return rest.join(QLatin1Char(' '));
}

QStringList bodyLines(const ContentItem &item)
{
    QStringList lines;
    for (const QString &line : item.text.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            lines << line.trimmed();
    }
    return lines;
}

// Draws `pixmap` to fill `bounds`, cropping like CSS "cover".
void drawCover(QPainter &painter, const QRectF &bounds, const QPixmap &pixmap)
{
    const QSizeF source = pixmap.size();
    const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
    const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
    painter.drawPixmap(bounds, pixmap,
                       QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
}

QFont pixelFont(const QFont &base, double size, QFont::Weight weight)
{
    QFont font = base;
    font.setPixelSize(qMax(1, qRound(size)));
    font.setWeight(weight);
    return font;
}

// Height of `text` wrapped to `width`, capped at `maxLines`.
qreal wrappedHeight(const QFont &font, const QString &text, qreal width, int maxLines)
{
    const QFontMetricsF metrics(font);
    const qreal full = metrics.boundingRect(QRectF(0, 0, width, 10000), Qt::TextWordWrap, text).height();
    return qMin(full, metrics.lineSpacing() * maxLines);
}

// design.pen "Type Pill": padding [4, 10], radius 12, 11.5/500.
void drawPill(QPainter &painter, const QPointF &topRight, bool withBackground)
{
    const QString label = withBackground ? QObject::tr("С фоном") : QObject::tr("Только текст");
    const QFont font = pixelFont(painter.font(), 11.5, QFont::Medium);
    const QFontMetricsF metrics(font);
    const QRectF pill(topRight.x() - metrics.horizontalAdvance(label) - 20, topRight.y(), metrics.horizontalAdvance(label) + 20,
                      metrics.height() + 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(withBackground ? QColor(Theme::AccentBlue) : QColor(Theme::BorderLight));
    painter.drawRoundedRect(pill, Theme::radius(12), Theme::radius(12));
    painter.setFont(font);
    painter.setPen(withBackground ? QColor(Qt::white) : QColor(Theme::TextDarkPrimary));
    painter.drawText(pill, Qt::AlignCenter, label);
}

// Shared behaviour of the grid card and the list row.
class AnnouncementItemWidget : public QWidget {
public:
    explicit AnnouncementItemWidget(const ContentItem &item)
        : m_item(item)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::ClickFocus);
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
    virtual QRectF menuRect() const = 0;

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (onClick)
            onClick();
        if (menuRect().adjusted(-4, -4, 4, 4).contains(event->position()) && onMenu)
            onMenu(mapToGlobal(menuRect().bottomLeft().toPoint() + QPoint(0, 6)));
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !menuRect().adjusted(-4, -4, 4, 4).contains(event->position()) && onDoubleClick)
            onDoubleClick();
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onClick)
            onClick();
        if (onMenu)
            onMenu(event->globalPos());
    }

    ContentItem m_item;
    bool m_selected = false;
};

// Grid card (design.pen "Ann …"): the background photo under a dark
// gradient, or a light grey card for text-only ones; title 22/800 and the
// lines 14.5/500 centred vertically, "⋯" top right, type pill bottom right.
class AnnouncementCard : public AnnouncementItemWidget {
public:
    using AnnouncementItemWidget::AnnouncementItemWidget;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qRound(width * CardRatio); }
    QSize sizeHint() const override { return QSize(268, 187); }

protected:
    QRectF menuRect() const override { return QRectF(width() - 38, 10, 28, 28); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect());
        const bool photo = hasBackground(m_item);

        QPainterPath clip;
        clip.addRoundedRect(bounds, Theme::radius(10), Theme::radius(10));
        painter.save();
        painter.setClipPath(clip);
        if (photo) {
            const QPixmap pixmap = backgroundPixmap(m_item);
            if (pixmap.isNull())
                painter.fillRect(bounds, QColor(0x2a, 0x30, 0x40));
            else
                drawCover(painter, bounds, pixmap);
            // design.pen "Shade": #000000b3 → #00000026.
            QLinearGradient shade(bounds.topLeft(), bounds.bottomLeft());
            shade.setColorAt(0, QColor(0, 0, 0, 179));
            shade.setColorAt(1, QColor(0, 0, 0, 38));
            painter.fillRect(bounds, shade);
        } else {
            painter.fillRect(bounds, QColor(Theme::SurfaceAlt));
        }
        if (underMouse())
            painter.fillRect(bounds, photo ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 6));
        painter.restore();
        if (!photo) {
            painter.setPen(QPen(QColor(Theme::BorderLight), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), Theme::radius(10), Theme::radius(10));
        }

        // Text block: padding [18, 18, 14, 22], gap 6, vertically centred.
        const double k = qBound(0.8, width() / 268.0, 1.1);
        const QRectF area = bounds.adjusted(22, 18, -18, -14);
        const QFont titleFont = pixelFont(painter.font(), 22 * k, QFont::ExtraBold);
        const QFont lineFont = pixelFont(painter.font(), 14.5 * k, QFont::Medium);
        const QString title = m_item.title.isEmpty() ? QObject::tr("Без названия") : m_item.title;
        const QStringList lines = bodyLines(m_item);

        struct Block {
            QString text;
            QFont font;
            qreal height;
            QColor color;
        };
        QList<Block> blocks;
        blocks.append({title, titleFont, wrappedHeight(titleFont, title, area.width(), 2),
                       photo ? QColor(Qt::white) : QColor(Theme::TextDarkPrimary)});
        for (const QString &line : lines)
            blocks.append({line, lineFont, wrappedHeight(lineFont, line, area.width(), 2),
                           photo ? QColor(Theme::SurfaceMuted) : QColor(Theme::TextDarkPrimary)});
        qreal total = 0;
        for (const Block &block : blocks)
            total += block.height;
        total += 6 * (blocks.size() - 1);

        qreal y = area.top() + qMax<qreal>(0, (area.height() - total) / 2);
        painter.save();
        painter.setClipRect(area);
        for (const Block &block : std::as_const(blocks)) {
            painter.setFont(block.font);
            painter.setPen(block.color);
            painter.drawText(QRectF(area.left(), y, area.width(), block.height), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                             block.text);
            y += block.height + 6;
        }
        painter.restore();

        // "More Button": 28×28, radius 7.
        const QRectF more = menuRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(photo ? QColor(0, 0, 0, 89) : QColor(Theme::BorderLight));
        painter.drawRoundedRect(more, Theme::radius(7), Theme::radius(7));
        painter.drawPixmap(QPointF(more.center().x() - 8, more.center().y() - 8),
                           IconProvider::pixmap(QStringLiteral("ellipsis"), photo ? QColor(Qt::white) : QColor(Theme::TextDarkPrimary), 16));

        drawPill(painter, QPointF(bounds.right() - 12, bounds.bottom() - 36), photo);

        if (m_selected) {
            painter.setPen(QPen(QColor(Theme::AccentBlue), 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(1.5, 1.5, -1.5, -1.5), Theme::radius(8.5), Theme::radius(8.5));
        }
    }
};

// List row: thumbnail, title, lines, type pill, "⋯".
class AnnouncementRow : public AnnouncementItemWidget {
public:
    explicit AnnouncementRow(const ContentItem &item)
        : AnnouncementItemWidget(item)
    {
        setFixedHeight(72);
    }

protected:
    QRectF menuRect() const override { return QRectF(width() - 40, (height() - 26) / 2.0, 26, 26); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(m_selected ? QColor(Theme::AccentBlue) : QColor(Theme::BorderLight), m_selected ? 1.5 : 1));
        painter.setBrush(m_selected ? QColor(Theme::AccentBlueBg) : (underMouse() ? QColor(Theme::SurfaceSubtle) : QColor(Theme::BgWhite)));
        painter.drawRoundedRect(bounds, Theme::radius(10), Theme::radius(10));

        const QRectF thumb(10, 8, 90, 56);
        QPainterPath clip;
        clip.addRoundedRect(thumb, Theme::radius(7), Theme::radius(7));
        painter.save();
        painter.setClipPath(clip);
        const QPixmap pixmap = backgroundPixmap(m_item);
        if (!pixmap.isNull()) {
            drawCover(painter, thumb, pixmap);
        } else {
            painter.fillRect(thumb, QColor(Theme::SurfaceAlt));
            painter.drawPixmap(QPointF(thumb.center().x() - 10, thumb.center().y() - 10),
                               IconProvider::pixmap(hasBackground(m_item) ? QStringLiteral("image") : QStringLiteral("file-text"),
                                                    QColor(Theme::Placeholder), 20));
        }
        painter.restore();

        const bool photo = hasBackground(m_item);
        const QString pillLabel = photo ? QObject::tr("С фоном") : QObject::tr("Только текст");
        const qreal pillWidth = QFontMetricsF(pixelFont(painter.font(), 11.5, QFont::Medium)).horizontalAdvance(pillLabel) + 20;
        const qreal textLeft = 116;
        const qreal textWidth = qMax<qreal>(40, width() - textLeft - pillWidth - 70);

        painter.setFont(pixelFont(painter.font(), 14, QFont::DemiBold));
        painter.setPen(QColor(m_selected ? Theme::AccentBlue : Theme::TextDarkPrimary));
        painter.drawText(QRectF(textLeft, 14, textWidth, 22), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_item.title, Qt::ElideRight, int(textWidth)));
        painter.setFont(pixelFont(painter.font(), 12, QFont::Medium));
        painter.setPen(QColor(Theme::TextDarkSecondary));
        painter.drawText(QRectF(textLeft, 38, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(bodyLines(m_item).join(QStringLiteral(" · ")), Qt::ElideRight, int(textWidth)));

        const qreal pillHeight = QFontMetricsF(pixelFont(painter.font(), 11.5, QFont::Medium)).height() + 8;
        drawPill(painter, QPointF(width() - 50, (height() - pillHeight) / 2), photo);
        painter.drawPixmap(QPointF(menuRect().center().x() - 9, menuRect().center().y() - 9),
                           IconProvider::pixmap(QStringLiteral("ellipsis"), QColor(Theme::TextDarkSecondary), 18));
    }
};

// ---- Editor building blocks -------------------------------------------------

QString outlineStyle(int radius)
{
    return QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: %2px;").arg(Theme::BorderLight).arg(radius);
}

// A 46px action button (design.pen "Replace Background" / "Save Button").
ClickFrame *actionButton(const QString &iconName, const QString &label, bool primary, QLabel **labelOut = nullptr)
{
    auto *button = new ClickFrame;
    button->setFixedHeight(46);
    styleFrame(button, QStringLiteral("AnnAction"),
               primary ? QStringLiteral("background: %1; border: none; border-radius: 9px;").arg(Theme::AccentBlue) : outlineStyle(9));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(7);
    layout->addStretch();
    const QString color = primary ? Theme::TextLightPrimary : Theme::TextDarkPrimary;
    layout->addWidget(icon(iconName, color, iconName == QStringLiteral("eye") || iconName == QStringLiteral("check") ? 17 : 16));
    auto *labelWidget = text(label, 13.5, primary ? 600 : 500, color);
    layout->addWidget(labelWidget);
    layout->addStretch();
    if (labelOut)
        *labelOut = labelWidget;
    return button;
}

// 36×36 outlined icon button (editor header).
ClickFrame *headerButton(const QString &iconName, const QString &tooltip)
{
    auto *button = new ClickFrame;
    button->setFixedSize(36, 36);
    button->setToolTip(tooltip);
    styleFrame(button, QStringLiteral("AnnHeaderButton"), outlineStyle(8));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(icon(iconName, Theme::TextDarkPrimary, 16), 0, Qt::AlignCenter);
    return button;
}

// A checkable option (design.pen "Type …" cards and "Option n" buttons):
// accent tint + 1.5px blue border when checked.
class OptionButton : public ClickFrame {
public:
    OptionButton(const QString &iconName, const QString &title, const QString &description, int radius)
        : m_iconName(iconName)
        , m_radius(radius)
    {
        setCursor(Qt::PointingHandCursor);
        auto *layout = new QHBoxLayout(this);
        if (description.isEmpty()) {
            setFixedHeight(40);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addStretch();
        } else {
            layout->setContentsMargins(10, 10, 10, 10);
            layout->setSpacing(10);
        }
        if (!iconName.isEmpty()) {
            m_icon = new QLabel;
            m_icon->setFixedSize(description.isEmpty() ? 18 : 20, description.isEmpty() ? 18 : 20);
            m_icon->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
            layout->addWidget(m_icon);
        }
        if (!title.isEmpty()) {
            auto *column = new QVBoxLayout;
            column->setSpacing(2);
            m_title = new QLabel(title);
            if (!description.isEmpty()) {
                m_title->setWordWrap(true);
                m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            } else {
                m_title->setAlignment(Qt::AlignCenter);
            }
            column->addWidget(m_title);
            if (!description.isEmpty()) {
                m_description = new QLabel(description);
                m_description->setWordWrap(true);
                // Wrap to whatever width the card gets instead of forcing
                // the editor wider than its scroll area.
                m_description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                column->addWidget(m_description);
            }
            layout->addLayout(column, 1);
        }
        if (description.isEmpty())
            layout->addStretch();
        m_titleSize = description.isEmpty() ? 16 : 13;
        setChecked(false);
    }

    void setTitleSize(double size)
    {
        m_titleSize = size;
        setChecked(m_checked);
    }

    void setChecked(bool checked)
    {
        m_checked = checked;
        const QString color = checked ? Theme::AccentBlue : Theme::TextDarkPrimary;
        styleFrame(this, QStringLiteral("AnnOption"),
                   QStringLiteral("background: %1; border: %2px solid %3; border-radius: %4px;")
                       .arg(checked ? Theme::AccentBlueBg : QStringLiteral("#ffffff"), checked ? QStringLiteral("1.5") : QStringLiteral("1"),
                            checked ? Theme::AccentBlue : Theme::BorderLight)
                       .arg(m_radius));
        if (m_icon)
            m_icon->setPixmap(IconProvider::pixmap(m_iconName, QColor(color), m_icon->width()));
        if (m_title)
            m_title->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: %2px; font-weight: %3;")
                                       .arg(color).arg(m_titleSize).arg(m_description ? 700 : 500));
        if (m_description)
            m_description->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: 10.5px;")
                                             .arg(checked ? Theme::AccentBlue : Theme::TextDarkSecondary));
    }

private:
    QString m_iconName;
    int m_radius;
    double m_titleSize = 13;
    bool m_checked = false;
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_description = nullptr;
};

// design.pen "Background Thumb": the chosen background, or a hint to pick one.
class BackgroundPreview : public ClickFrame {
public:
    BackgroundPreview()
    {
        setFixedHeight(100);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("Выбрать фон"));
    }

    void setBackground(BackgroundType type, const QString &path)
    {
        m_item.backgroundType = type;
        m_item.backgroundPath = path;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect());
        QPainterPath clip;
        clip.addRoundedRect(bounds, Theme::radius(8), Theme::radius(8));
        painter.setClipPath(clip);
        const QPixmap pixmap = backgroundPixmap(m_item);
        if (!pixmap.isNull()) {
            drawCover(painter, bounds, pixmap);
            return;
        }
        painter.fillRect(bounds, QColor(Theme::SurfaceMuted));
        const bool pending = hasBackground(m_item);
        painter.drawPixmap(QPointF(bounds.center().x() - 11, bounds.center().y() - 20),
                           IconProvider::pixmap(QStringLiteral("image-plus"), QColor(Theme::Placeholder), 22));
        painter.setFont(pixelFont(painter.font(), 11.5, QFont::Medium));
        painter.setPen(QColor(Theme::TextDarkSecondary));
        painter.drawText(QRectF(0, bounds.center().y() + 6, bounds.width(), 18), Qt::AlignCenter,
                         pending ? QObject::tr("Загрузка…") : QObject::tr("Фон не выбран"));
    }

private:
    ContentItem m_item;
};

QPixmap dotPixmap(const QString &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(color));
    painter.drawEllipse(QRectF(4, 4, 8, 8));
    return pixmap;
}

} // namespace

// ---------------------------------------------------------------------------
// "Редактирование объявления" (design.pen "Editor Card"): width 390,
// padding 16, gap 12, radius 12.

class AnnouncementEditor : public QFrame {
    Q_DECLARE_TR_FUNCTIONS(AnnouncementEditor)

public:
    AnnouncementEditor();

    // design.pen "Editor Card" is 390 wide; it gives way first when the
    // window is narrow (down to its minimum).
    QSize sizeHint() const override { return QSize(390, QFrame::sizeHint().height()); }

    void setItem(const ContentItem &item);
    ContentItem draft() const;
    bool isDirty() const;
    bool isNew() const { return m_original.id < 0; }
    int itemId() const { return m_original.id; }
    void focusTitle();
    // Fits the action row into `innerWidth`.
    void setCompact(int innerWidth);

    std::function<void(const ContentItem &)> onSave;
    std::function<void(const ContentItem &)> onPreview;
    std::function<void()> onDelete;
    std::function<void(const QPoint &)> onMore;

private:
    QWidget *field(const QString &label, int limit, QLineEdit **edit, QLabel **counter);
    void setWithBackground(bool with);
    void setAlign(TextAlign align);
    void setSize(int size);
    void setCategory(const QString &name);
    void chooseBackground();
    void refreshState();

    ContentItem m_original;
    bool m_withBackground = false;
    BackgroundType m_backgroundType = BackgroundType::Photo;
    QString m_backgroundPath;
    TextAlign m_align = TextAlign::Center;
    int m_size = 0;
    QString m_category;

    QLabel *m_heading = nullptr;
    ClickFrame *m_deleteButton = nullptr;
    OptionButton *m_textOnly = nullptr;
    OptionButton *m_withImage = nullptr;
    QWidget *m_backgroundSection = nullptr;
    BackgroundPreview *m_preview = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_subtitle = nullptr;
    QLineEdit *m_extra = nullptr;
    QLabel *m_titleCounter = nullptr;
    QLabel *m_subtitleCounter = nullptr;
    QLabel *m_extraCounter = nullptr;
    QList<OptionButton *> m_alignButtons;
    QList<OptionButton *> m_sizeButtons;
    QFrame *m_categoryDot = nullptr;
    QLabel *m_categoryValue = nullptr;
    ClickFrame *m_saveButton = nullptr;
    QLabel *m_saveLabel = nullptr;
    QLabel *m_previewLabel = nullptr;
    int m_innerWidth = 358;
};

AnnouncementEditor::AnnouncementEditor()
{
    setObjectName(QStringLiteral("AnnouncementEditor"));
    setStyleSheet(QStringLiteral("QFrame#AnnouncementEditor { background: #ffffff; border: 1px solid %1; border-radius: 12px; }").arg(Theme::BorderLight));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---- Header ----
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    m_heading = new ElideLabel(tr("Редактирование объявления"));
    m_heading->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: 16px; font-weight: 700;")
                                 .arg(Theme::TextDarkPrimary));
    header->addWidget(m_heading, 1);
    m_deleteButton = headerButton(QStringLiteral("trash-2"), tr("Удалить объявление"));
    m_deleteButton->onClick = [this]() {
        if (onDelete)
            onDelete();
    };
    header->addWidget(m_deleteButton);
    ClickFrame *more = headerButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"));
    more->onClick = [this, more]() {
        if (onMore)
            onMore(more->mapToGlobal(QPoint(0, more->height() + 4)));
    };
    header->addWidget(more);
    root->addLayout(header);

    // ---- Scrollable fields ----
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }") + Theme::scrollBarCss());
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("AnnEditorBody"));
    body->setStyleSheet(QStringLiteral("QWidget#AnnEditorBody { background: #ffffff; }"));
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(12);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
    // Word-wrapped labels make the body height-for-width, and QScrollArea
    // then sizes it before the vertical scrollbar takes its share — cap it
    // at the viewport so nothing is cut off on the right.
    struct WidthCap : QObject {
        QWidget *body;
        WidthCap(QObject *parent, QWidget *b) : QObject(parent), body(b) {}
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                body->setMaximumWidth(static_cast<QWidget *>(watched)->width());
            return false;
        }
    };
    scroll->viewport()->installEventFilter(new WidthCap(scroll, body));

    const auto label = [](const QString &value) { return text(value, 13, 500, Theme::TextDarkPrimary); };

    layout->addWidget(label(tr("Тип объявления")));
    auto *types = new QHBoxLayout;
    types->setSpacing(8);
    m_textOnly = new OptionButton(QStringLiteral("file-text"), tr("Только текст"), tr("Простой текст без изображения"), 9);
    m_withImage = new OptionButton(QStringLiteral("image"), tr("Фон + текст"), tr("Фоновое изображение с текстом"), 9);
    m_textOnly->onClick = [this]() { setWithBackground(false); };
    m_withImage->onClick = [this]() { setWithBackground(true); };
    types->addWidget(m_textOnly, 1);
    types->addWidget(m_withImage, 1);
    layout->addLayout(types);

    m_backgroundSection = new QWidget;
    auto *backgroundLayout = new QVBoxLayout(m_backgroundSection);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    backgroundLayout->setSpacing(12);
    backgroundLayout->addWidget(label(tr("Фоновое изображение")));
    auto *backgroundRow = new QHBoxLayout;
    backgroundRow->setSpacing(10);
    m_preview = new BackgroundPreview;
    m_preview->onClick = [this]() { chooseBackground(); };
    backgroundRow->addWidget(m_preview, 1);
    auto *backgroundButtons = new QVBoxLayout;
    backgroundButtons->setSpacing(8);
    ClickFrame *replace = actionButton(QStringLiteral("image"), tr("Заменить фон"), true);
    replace->onClick = [this]() { chooseBackground(); };
    ClickFrame *reset = actionButton(QStringLiteral("trash-2"), tr("Сбросить фон"), false);
    reset->onClick = [this]() {
        m_backgroundPath.clear();
        refreshState();
    };
    backgroundButtons->addWidget(replace);
    backgroundButtons->addWidget(reset);
    backgroundRow->addLayout(backgroundButtons, 1);
    backgroundLayout->addLayout(backgroundRow);
    auto *hint = text(tr("Рекомендуемый размер: 1920×1080 (16:9)"), 10.5, 400, Theme::TextDarkSecondary);
    backgroundLayout->addWidget(hint);
    layout->addWidget(m_backgroundSection);

    layout->addWidget(field(tr("Заголовок"), 32, &m_title, &m_titleCounter));
    layout->addWidget(field(tr("Подзаголовок"), 40, &m_subtitle, &m_subtitleCounter));
    layout->addWidget(field(tr("Дополнительный текст"), 60, &m_extra, &m_extraCounter));

    // ---- Alignment / size ----
    auto *alignSize = new QHBoxLayout;
    alignSize->setSpacing(14);
    const auto group = [&](const QString &title, QList<OptionButton *> &buttons, const QList<QPair<QString, QString>> &items) {
        auto *column = new QVBoxLayout;
        column->setSpacing(6);
        column->addWidget(label(title));
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        for (const auto &item : items) {
            auto *button = new OptionButton(item.first, item.second, QString(), 8);
            buttons << button;
            row->addWidget(button, 1);
        }
        column->addLayout(row);
        alignSize->addLayout(column, 1);
    };
    group(tr("Выравнивание"), m_alignButtons,
          {{QStringLiteral("align-left"), QString()}, {QStringLiteral("align-center"), QString()}, {QStringLiteral("align-right"), QString()}});
    group(tr("Размер текста"), m_sizeButtons,
          {{QString(), QStringLiteral("A⁻")}, {QString(), QStringLiteral("A")}, {QString(), QStringLiteral("A⁺")}});
    m_alignButtons.at(0)->setToolTip(tr("По левому краю"));
    m_alignButtons.at(1)->setToolTip(tr("По центру"));
    m_alignButtons.at(2)->setToolTip(tr("По правому краю"));
    m_sizeButtons.at(0)->setToolTip(tr("Мельче"));
    m_sizeButtons.at(1)->setToolTip(tr("Обычный"));
    m_sizeButtons.at(2)->setToolTip(tr("Крупнее"));
    m_sizeButtons.at(0)->setTitleSize(13);
    m_sizeButtons.at(2)->setTitleSize(18);
    const TextAlign aligns[] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
    for (int i = 0; i < 3; ++i) {
        const TextAlign align = aligns[i];
        m_alignButtons.at(i)->onClick = [this, align]() { setAlign(align); };
        m_sizeButtons.at(i)->onClick = [this, i]() { setSize(i - 1); };
    }
    layout->addLayout(alignSize);

    // ---- Category ----
    auto *categoryRow = new QHBoxLayout;
    categoryRow->setSpacing(14);
    categoryRow->addWidget(label(tr("Категория")));
    auto *dropdown = new ClickFrame;
    dropdown->setFixedHeight(38);
    dropdown->setCursor(Qt::PointingHandCursor);
    styleFrame(dropdown, QStringLiteral("AnnCategory"), outlineStyle(8));
    auto *dropdownLayout = new QHBoxLayout(dropdown);
    dropdownLayout->setContentsMargins(12, 0, 12, 0);
    dropdownLayout->setSpacing(10);
    m_categoryDot = new QFrame;
    m_categoryDot->setFixedSize(8, 8);
    dropdownLayout->addWidget(m_categoryDot);
    m_categoryValue = text(QString(), 13.5, 500, Theme::TextDarkPrimary);
    dropdownLayout->addWidget(m_categoryValue, 1);
    dropdownLayout->addWidget(icon(QStringLiteral("chevron-down"), Theme::TextDarkSecondary, 15));
    dropdown->onClick = [this, dropdown]() {
        QMenu menu(this);
        for (const Category &category : categories()) {
            QAction *action = menu.addAction(QIcon(dotPixmap(category.color)), category.name);
            const QString name = category.name;
            connect(action, &QAction::triggered, this, [this, name]() { setCategory(name); });
        }
        menu.setMinimumWidth(dropdown->width());
        menu.exec(dropdown->mapToGlobal(QPoint(0, dropdown->height() + 2)));
    };
    categoryRow->addWidget(dropdown, 1);
    layout->addLayout(categoryRow);
    layout->addStretch();

    // ---- Actions ----
    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    QLabel *previewLabel = nullptr;
    ClickFrame *preview = actionButton(QStringLiteral("eye"), tr("Предпросмотр"), false, &previewLabel);
    preview->setToolTip(tr("Предпросмотр"));
    preview->onClick = [this]() {
        if (onPreview)
            onPreview(draft());
    };
    actions->addWidget(preview);
    m_saveButton = actionButton(QStringLiteral("check"), tr("Сохранить изменения"), true, &m_saveLabel);
    m_saveButton->onClick = [this]() {
        if (m_title->text().trimmed().isEmpty()) {
            QMessageBox::information(this, tr("Объявление"), tr("Введите заголовок объявления."));
            m_title->setFocus();
            return;
        }
        if (onSave)
            onSave(draft());
    };
    actions->addWidget(m_saveButton, 1);
    root->addLayout(actions);

    // Narrower than the design's 390: "Предпросмотр" drops to its icon,
    // then "Сохранить изменения" to "Сохранить".
    struct ActionCollapser : QObject {
        AnnouncementEditor *editor;
        QLabel *preview;
        ActionCollapser(AnnouncementEditor *e, QLabel *p) : QObject(e), editor(e), preview(p) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                editor->setCompact(editor->width() - 32);
            return false;
        }
    };
    m_previewLabel = previewLabel;
    installEventFilter(new ActionCollapser(this, previewLabel));

    for (QLineEdit *edit : {m_title, m_subtitle, m_extra})
        connect(edit, &QLineEdit::textChanged, this, [this]() { refreshState(); });

    ContentItem empty;
    empty.type = ContentType::Announcement;
    setItem(empty);
}

QWidget *AnnouncementEditor::field(const QString &label, int limit, QLineEdit **edit, QLabel **counter)
{
    auto *column = new QWidget;
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *labelRow = new QHBoxLayout;
    labelRow->addWidget(text(label, 13, 500, Theme::TextDarkPrimary));
    labelRow->addStretch();
    *counter = text(QString::number(limit), 11.5, 500, Theme::TextDarkSecondary);
    (*counter)->setProperty("limit", limit);
    labelRow->addWidget(*counter);
    layout->addLayout(labelRow);
    *edit = new QLineEdit;
    (*edit)->setFixedHeight(36);
    (*edit)->setMaxLength(limit);
    (*edit)->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; border: 1px solid %1; border-radius: 8px; padding: 0 12px; font-size: 13.5px; color: %2; }"
        "QLineEdit:focus { border: 1px solid %3; }").arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue));
    layout->addWidget(*edit);
    return column;
}

void AnnouncementEditor::setItem(const ContentItem &item)
{
    m_original = item;
    m_original.type = ContentType::Announcement;
    m_withBackground = item.backgroundType != BackgroundType::None;
    m_backgroundType = item.backgroundType == BackgroundType::Video ? BackgroundType::Video : BackgroundType::Photo;
    m_backgroundPath = m_withBackground ? item.backgroundPath : QString();
    m_align = item.textAlign;
    m_size = item.textSize;
    m_category = item.category.isEmpty() ? categories().first().name : item.category;

    const QString subtitle = subtitleOf(item);
    const QString extra = extraOf(item);
    // Older announcements can be longer than the limits: never cut them.
    const QList<QPair<QLineEdit *, QString>> values = {{m_title, item.title}, {m_subtitle, subtitle}, {m_extra, extra}};
    const int limits[] = {32, 40, 60};
    for (int i = 0; i < values.size(); ++i) {
        QLineEdit *edit = values.at(i).first;
        const QSignalBlocker blocker(edit);
        edit->setMaxLength(qMax(limits[i], int(values.at(i).second.size())));
        edit->setText(values.at(i).second);
        edit->setCursorPosition(0);
    }
    m_heading->setText(isNew() ? tr("Новое объявление") : tr("Редактирование объявления"));
    m_deleteButton->setVisible(!isNew());
    setCompact(m_innerWidth);
    setCategory(m_category);
    refreshState();
}

ContentItem AnnouncementEditor::draft() const
{
    ContentItem item = m_original;
    item.type = ContentType::Announcement;
    item.title = m_title->text().trimmed();
    QStringList lines;
    if (!m_subtitle->text().trimmed().isEmpty() || !m_extra->text().trimmed().isEmpty())
        lines << m_subtitle->text().trimmed();
    if (!m_extra->text().trimmed().isEmpty())
        lines << m_extra->text().trimmed();
    item.text = lines.join(QLatin1Char('\n'));
    item.backgroundType = m_withBackground && !m_backgroundPath.isEmpty() ? m_backgroundType : BackgroundType::None;
    item.backgroundPath = item.backgroundType == BackgroundType::None ? QString() : m_backgroundPath;
    item.textAlign = m_align;
    item.textSize = m_size;
    item.category = m_category;
    return item;
}

bool AnnouncementEditor::isDirty() const
{
    const ContentItem current = draft();
    if (isNew())
        return !current.title.isEmpty() || !current.text.isEmpty() || hasBackground(current);
    const auto normalized = [](const ContentItem &item) {
        return QStringList{item.title.trimmed(), item.text.trimmed(), backgroundTypeToDbString(item.backgroundType),
                           item.backgroundType == BackgroundType::None ? QString() : item.backgroundPath,
                           QString::number(int(item.textAlign)), QString::number(item.textSize),
                           item.category.isEmpty() ? categories().first().name : item.category};
    };
    return normalized(current) != normalized(m_original);
}

void AnnouncementEditor::setCompact(int innerWidth)
{
    m_innerWidth = innerWidth;
    const bool fullPreview = innerWidth >= 351;
    m_previewLabel->setVisible(fullPreview);
    const int saveRoom = innerWidth - 10 - (fullPreview ? 143 : 46);
    if (isNew())
        m_saveLabel->setText(saveRoom >= 190 ? tr("Создать объявление") : tr("Создать"));
    else
        m_saveLabel->setText(saveRoom >= 198 ? tr("Сохранить изменения") : tr("Сохранить"));
}

void AnnouncementEditor::focusTitle()
{
    m_title->setFocus();
    m_title->selectAll();
}

void AnnouncementEditor::setWithBackground(bool with)
{
    m_withBackground = with;
    refreshState();
    if (with && m_backgroundPath.isEmpty())
        chooseBackground();
}

void AnnouncementEditor::setAlign(TextAlign align)
{
    m_align = align;
    refreshState();
}

void AnnouncementEditor::setSize(int size)
{
    m_size = qBound(-1, size, 1);
    refreshState();
}

void AnnouncementEditor::setCategory(const QString &name)
{
    m_category = name;
    m_categoryValue->setText(name);
    m_categoryDot->setStyleSheet(QStringLiteral("background: %1; border: none; border-radius: 4px;").arg(categoryColor(name)));
    refreshState();
}

void AnnouncementEditor::chooseBackground()
{
    QStringList patterns;
    for (const QString &ext : {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"),
                               QStringLiteral("webp")})
        patterns << QStringLiteral("*.") + ext;
    QStringList videoPatterns;
    for (const QString &ext : videoExtensions())
        videoPatterns << QStringLiteral("*.") + ext;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Фон объявления"), QString(),
        tr("Изображения и видео (%1 %2);;Изображения (%1);;Видео (%2)")
            .arg(patterns.join(QLatin1Char(' ')), videoPatterns.join(QLatin1Char(' '))));
    if (path.isEmpty())
        return;
    m_backgroundPath = path;
    m_backgroundType = videoExtensions().contains(QFileInfo(path).suffix().toLower()) ? BackgroundType::Video : BackgroundType::Photo;
    m_withBackground = true;
    refreshState();
}

void AnnouncementEditor::refreshState()
{
    m_textOnly->setChecked(!m_withBackground);
    m_withImage->setChecked(m_withBackground);
    m_backgroundSection->setVisible(m_withBackground);
    m_preview->setBackground(m_backgroundPath.isEmpty() ? BackgroundType::None : m_backgroundType, m_backgroundPath);
    const TextAlign aligns[] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
    for (int i = 0; i < 3; ++i) {
        m_alignButtons.at(i)->setChecked(m_align == aligns[i]);
        m_sizeButtons.at(i)->setChecked(m_size == i - 1);
    }
    for (const auto &pair : {qMakePair(m_title, m_titleCounter), qMakePair(m_subtitle, m_subtitleCounter), qMakePair(m_extra, m_extraCounter)})
        pair.second->setText(QStringLiteral("%1/%2").arg(pair.first->text().size()).arg(pair.second->property("limit").toInt()));

    const bool dirty = isDirty();
    styleFrame(m_saveButton, QStringLiteral("AnnAction"),
               QStringLiteral("background: %1; border: none; border-radius: 9px;").arg(dirty || isNew() ? Theme::AccentBlue : QStringLiteral("#9db8f2")));
    m_saveButton->setToolTip(dirty ? QString() : tr("Изменений нет"));
}

// ---------------------------------------------------------------------------

AnnouncementsPanel::AnnouncementsPanel(ContentRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_repository(repository)
{
    setObjectName(QStringLiteral("AnnouncementsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#AnnouncementsPanel { background: #ffffff; }"));
    m_listView = QSettings().value(QStringLiteral("announcements/listView"), false).toBool();

    // design.pen "Announcements Content": vertical, gap 18, padding [22, 24, 16, 24].
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 16);
    layout->setSpacing(18);
    layout->addWidget(text(tr("Объявления"), 30, 800, Theme::TextDarkPrimary));
    layout->addWidget(buildToolbar());

    // "Body Row": the grid next to the editor, gap 16.
    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    m_stack = new QStackedWidget;
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    m_itemsWidget = new QWidget;
    m_itemsWidget->setObjectName(QStringLiteral("AnnouncementItems"));
    m_itemsWidget->setStyleSheet(QStringLiteral("QWidget#AnnouncementItems { background: #ffffff; }"));
    m_scroll->setWidget(m_itemsWidget);
    m_stack->addWidget(m_scroll);

    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(10);
    auto *emptyIcon = new QLabel;
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(IconProvider::pixmap(QStringLiteral("megaphone"), QColor(Theme::Placeholder), 44));
    emptyLayout->addWidget(emptyIcon);
    m_emptyTitle = text(QString(), 17, 700, Theme::TextDarkPrimary);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyHint = text(QString(), 13.5, 500, Theme::TextDarkSecondary);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    emptyLayout->addWidget(m_emptyHint);
    m_stack->addWidget(empty);
    body->addWidget(m_stack, 1);

    m_editor = new AnnouncementEditor;
    m_editor->setMinimumWidth(300);
    m_editor->setMaximumWidth(390);
    m_editor->onSave = [this](const ContentItem &item) { save(item); };
    m_editor->onPreview = [this](const ContentItem &item) { emit previewRequested(item); };
    m_editor->onDelete = [this]() {
        if (!m_editor->isNew())
            remove(m_editor->itemId());
    };
    m_editor->onMore = [this](const QPoint &pos) {
        if (m_editor->isNew())
            return;
        const int id = m_editor->itemId();
        QMenu menu(this);
        menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16), tr("Показать на экране"), this,
                       [this, id]() {
                           if (const ContentItem *item = announcement(id))
                               emit goLiveRequested(*item);
                       });
        menu.addAction(IconProvider::icon(QStringLiteral("copy"), QColor(Theme::TextDarkPrimary), 16), tr("Дублировать"), this,
                       [this, id]() { duplicate(id); });
        menu.exec(pos);
    };
    body->addWidget(m_editor);
    layout->addLayout(body, 1);

    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_scroll);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this]() {
        if (m_selectedId >= 0)
            remove(m_selectedId);
    });

    const auto repaintItems = [this]() {
        for (QWidget *widget : std::as_const(m_itemWidgets))
            widget->update();
        m_editor->update();
        for (QWidget *child : m_editor->findChildren<QFrame *>())
            child->update();
    };
    connect(ThumbnailCache::instance(), &ThumbnailCache::ready, this, repaintItems);
    connect(VideoThumbnailer::instance(), &VideoThumbnailer::ready, this, repaintItems);

    // "Автосохранение изменений": unsaved edits are saved on a timer.
    m_autosave = new QTimer(this);
    connect(m_autosave, &QTimer::timeout, this, [this]() {
        if (m_editor->isDirty() && !m_editor->draft().title.isEmpty())
            save(m_editor->draft());
    });
    applySettings();

    setListView(m_listView);
    reload();
}

void AnnouncementsPanel::applySettings()
{
    m_autosave->setInterval(qMax(10, AppSettings::value(AppSettings::AutosaveInterval).toInt()) * 1000);
    if (AppSettings::value(AppSettings::AutosaveEnabled).toBool())
        m_autosave->start();
    else
        m_autosave->stop();
}

QWidget *AnnouncementsPanel::buildToolbar()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    m_searchEdit->setPlaceholderText(tr("Поиск по названию..."));
    m_searchEdit->setFixedHeight(46);
    m_searchEdit->setMinimumWidth(120);
    m_searchEdit->addAction(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 17),
                            QLineEdit::LeadingPosition);
    m_searchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; border: 1px solid %1; border-radius: 9px; padding: 0 10px; font-size: 14px; color: %2; }"
        "QLineEdit:focus { border: 1px solid %3; }").arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &value) {
        m_search = value.trimmed();
        rebuildItems();
    });
    layout->addWidget(m_searchEdit, 1);

    // design.pen "Add Button": 46 tall, padding [0, 18], gap 9, 15/600.
    auto *add = new ClickFrame;
    add->setFixedHeight(46);
    styleFrame(add, QStringLiteral("AnnAdd"), QStringLiteral("background: %1; border: none; border-radius: 9px;").arg(Theme::AccentBlue));
    auto *addLayout = new QHBoxLayout(add);
    addLayout->setContentsMargins(18, 0, 18, 0);
    addLayout->setSpacing(9);
    addLayout->addWidget(icon(QStringLiteral("plus"), Theme::TextLightPrimary, 17));
    QLabel *addLabel = text(tr("Добавить объявление"), 15, 600, Theme::TextLightPrimary);
    addLayout->addWidget(addLabel);
    add->setToolTip(tr("Добавить объявление"));
    add->onClick = [this]() { createNew(); };
    // A narrow window can't fit search + labelled button + toggle + sort:
    // the button drops to its "+" icon.
    struct LabelCollapser : QObject {
        QWidget *row;
        QWidget *label;
        LabelCollapser(QWidget *r, QWidget *l) : QObject(r), row(r), label(l) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                label->setVisible(row->width() >= 700);
            return false;
        }
    };
    row->installEventFilter(new LabelCollapser(row, addLabel));
    layout->addWidget(add);

    // design.pen "View Toggle": two 46×46 segments, the active one tinted.
    auto *toggle = new QFrame;
    toggle->setFixedHeight(46);
    styleFrame(toggle, QStringLiteral("AnnViewToggle"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
    auto *toggleLayout = new QHBoxLayout(toggle);
    toggleLayout->setContentsMargins(1, 1, 1, 1);
    toggleLayout->setSpacing(0);
    m_gridButton = new ClickFrame;
    m_listButton = new ClickFrame;
    m_gridButton->setToolTip(tr("Плитка"));
    m_listButton->setToolTip(tr("Список"));
    for (ClickFrame *button : {m_gridButton, m_listButton}) {
        button->setFixedSize(45, 44);
        auto *buttonLayout = new QHBoxLayout(button);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        auto *iconLabel = new QLabel;
        iconLabel->setObjectName(QStringLiteral("ToggleIcon"));
        iconLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        buttonLayout->addWidget(iconLabel, 0, Qt::AlignCenter);
    }
    m_gridButton->onClick = [this]() { setListView(false); };
    m_listButton->onClick = [this]() { setListView(true); };
    toggleLayout->addWidget(m_gridButton);
    auto *divider = new QFrame;
    divider->setFixedWidth(1);
    divider->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(Theme::BorderLight));
    toggleLayout->addWidget(divider);
    toggleLayout->addWidget(m_listButton);
    layout->addWidget(toggle);

    m_sortField = new DropdownField(true);
    m_sortField->setOptions({tr("По дате (новые)"), tr("По дате (старые)"), tr("По названию")});
    m_sortField->setCurrent(0);
    m_sortField->setFixedHeight(46);
    m_sortField->setMinimumWidth(150);
    m_sortField->onSelected = [this](int index) {
        m_sort = index;
        rebuildItems();
    };
    layout->addWidget(m_sortField);
    return row;
}

void AnnouncementsPanel::setListView(bool list)
{
    m_listView = list;
    QSettings().setValue(QStringLiteral("announcements/listView"), list);
    const auto paint = [](ClickFrame *button, bool on, const QString &iconName, bool leftEdge) {
        button->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; border-top-%2-radius: 8px; border-bottom-%2-radius: 8px; }")
                                  .arg(on ? Theme::AccentBlueBg : QStringLiteral("#ffffff"),
                                       leftEdge ? QStringLiteral("left") : QStringLiteral("right")));
        button->findChild<QLabel *>(QStringLiteral("ToggleIcon"))
            ->setPixmap(IconProvider::pixmap(iconName, QColor(on ? Theme::AccentBlue : Theme::TextDarkSecondary), 18));
    };
    paint(m_gridButton, !list, QStringLiteral("layout-grid"), true);
    paint(m_listButton, list, QStringLiteral("list"), false);

    if (QLayout *old = m_itemsWidget->layout()) {
        while (QLayoutItem *item = old->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        delete old;
    }
    m_itemWidgets.clear();
    if (list) {
        auto *column = new QVBoxLayout(m_itemsWidget);
        column->setContentsMargins(0, 0, 0, 8);
        column->setSpacing(8);
        column->setAlignment(Qt::AlignTop);
    } else {
        // design.pen "Announcement Grid": 2 columns, gap 12.
        auto *flow = new FlowLayout(FlowLayout::Mode::EqualColumns, 12, 220, m_itemsWidget);
        flow->setContentsMargins(0, 0, 0, 8);
        flow->setColumnsFromWidthOnly(true);
    }
    rebuildItems();
}

// ---- Data ------------------------------------------------------------------

void AnnouncementsPanel::reload()
{
    // Expired ones too: this is where they're managed.
    m_items = m_repository->fetch(QString(), ContentType::Announcement, SortOrder::DateAddedDesc, true);
    rebuildItems();

    if (m_editor->isDirty())
        return;
    const ContentItem *current = announcement(m_selectedId);
    if (!current && m_selectedId >= 0) {
        m_selectedId = -1;
    }
    if (!current && m_editor->isNew() && !m_itemWidgets.isEmpty()) {
        // Nothing chosen yet: start on the first card.
        const QList<ContentItem> shown = visibleItems();
        if (!shown.isEmpty()) {
            m_selectedId = shown.first().id;
            current = announcement(m_selectedId);
        }
    }
    if (current) {
        m_editor->setItem(*current);
    } else {
        ContentItem draft;
        draft.type = ContentType::Announcement;
        m_editor->setItem(draft);
    }
    refreshSelection();
}

const ContentItem *AnnouncementsPanel::announcement(int id) const
{
    for (const ContentItem &item : m_items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

QList<ContentItem> AnnouncementsPanel::visibleItems() const
{
    QList<ContentItem> result;
    for (const ContentItem &item : m_items) {
        if (m_search.isEmpty() || item.title.contains(m_search, Qt::CaseInsensitive) || item.text.contains(m_search, Qt::CaseInsensitive))
            result << item;
    }
    std::stable_sort(result.begin(), result.end(), [this](const ContentItem &a, const ContentItem &b) {
        switch (m_sort) {
        case 1: return a.createdAt < b.createdAt;
        case 2: return QString::localeAwareCompare(a.title, b.title) < 0;
        default: return a.createdAt > b.createdAt;
        }
    });
    return result;
}

void AnnouncementsPanel::rebuildItems()
{
    QLayout *layout = m_itemsWidget->layout();
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_itemWidgets.clear();

    const QList<ContentItem> items = visibleItems();
    for (const ContentItem &item : items) {
        AnnouncementItemWidget *widget = m_listView ? static_cast<AnnouncementItemWidget *>(new AnnouncementRow(item))
                                                    : static_cast<AnnouncementItemWidget *>(new AnnouncementCard(item));
        if (!m_listView) {
            QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            widget->setSizePolicy(policy);
        }
        // Deferred: selecting can save and rebuild the grid, which must not
        // delete the card while it's still handling the mouse event.
        const int id = item.id;
        widget->onClick = [this, id]() { QTimer::singleShot(0, this, [this, id]() { select(id); }); };
        widget->onDoubleClick = [this, id]() {
            QTimer::singleShot(0, this, [this, id]() {
                if (const ContentItem *announcementItem = announcement(id))
                    emit goLiveRequested(*announcementItem);
            });
        };
        widget->onMenu = [this, id](const QPoint &pos) {
            QTimer::singleShot(0, this, [this, id, pos]() {
                if (m_selectedId == id)
                    showItemMenu(id, pos);
            });
        };
        layout->addWidget(widget);
        m_itemWidgets.insert(id, widget);
    }

    if (items.isEmpty()) {
        m_emptyTitle->setText(m_items.isEmpty() ? tr("Пока нет объявлений") : tr("Ничего не найдено"));
        m_emptyHint->setText(m_items.isEmpty() ? tr("Заполните карточку справа и нажмите «Создать объявление».")
                                               : tr("Попробуйте другой поисковый запрос."));
        m_stack->setCurrentIndex(1);
    } else {
        m_stack->setCurrentIndex(0);
    }
    refreshSelection();
    m_itemsWidget->updateGeometry();
}

void AnnouncementsPanel::refreshSelection()
{
    for (auto it = m_itemWidgets.constBegin(); it != m_itemWidgets.constEnd(); ++it)
        static_cast<AnnouncementItemWidget *>(it.value())->setSelected(it.key() == m_selectedId);
}

// ---- Interaction -----------------------------------------------------------

bool AnnouncementsPanel::confirmLeaveEditor()
{
    if (!m_editor->isDirty())
        return true;
    QMessageBox box(this);
    box.setWindowTitle(tr("Объявление"));
    box.setText(m_editor->isNew() ? tr("Сохранить новое объявление?") : tr("Сохранить изменения в объявлении?"));
    QPushButton *saveButton = box.addButton(tr("Сохранить"), QMessageBox::AcceptRole);
    QPushButton *discardButton = box.addButton(tr("Не сохранять"), QMessageBox::DestructiveRole);
    box.addButton(tr("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();
    if (box.clickedButton() == discardButton)
        return true;
    if (box.clickedButton() != saveButton)
        return false;
    const ContentItem item = m_editor->draft();
    if (item.title.isEmpty()) {
        QMessageBox::information(this, tr("Объявление"), tr("Введите заголовок объявления."));
        return false;
    }
    save(item);
    return true;
}

void AnnouncementsPanel::select(int id)
{
    if (id == m_selectedId && !m_editor->isNew())
        return;
    if (!confirmLeaveEditor())
        return;
    const ContentItem *item = announcement(id);
    if (!item)
        return;
    m_selectedId = id;
    refreshSelection();
    m_editor->setItem(*item);
}

void AnnouncementsPanel::createNew()
{
    if (!confirmLeaveEditor())
        return;
    m_selectedId = -1;
    refreshSelection();
    ContentItem draft;
    draft.type = ContentType::Announcement;
    m_editor->setItem(draft);
    m_editor->focusTitle();
}

void AnnouncementsPanel::goLiveSelected()
{
    if (const ContentItem *item = announcement(m_selectedId))
        emit goLiveRequested(*item);
}

void AnnouncementsPanel::showItemMenu(int id, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16), tr("Показать на экране"), this,
                   [this, id]() {
                       if (const ContentItem *item = announcement(id))
                           emit goLiveRequested(*item);
                   });
    menu.addAction(IconProvider::icon(QStringLiteral("eye"), QColor(Theme::TextDarkPrimary), 16), tr("Предпросмотр"), this,
                   [this, id]() {
                       if (const ContentItem *item = announcement(id))
                           emit previewRequested(*item);
                   });
    menu.addAction(IconProvider::icon(QStringLiteral("copy"), QColor(Theme::TextDarkPrimary), 16), tr("Дублировать"), this,
                   [this, id]() { duplicate(id); });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 16), tr("Удалить"), this,
                   [this, id]() { remove(id); });
    menu.exec(globalPos);
}

void AnnouncementsPanel::duplicate(int id)
{
    const ContentItem *source = announcement(id);
    if (!source || !confirmLeaveEditor())
        return;
    ContentItem copy = *source;
    copy.id = -1;
    copy.title = tr("%1 (копия)").arg(source->title);
    if (!m_repository->add(copy)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить запись."));
        return;
    }
    m_selectedId = copy.id;
    m_editor->setItem(copy);
    reload();
    emit libraryChanged();
}

void AnnouncementsPanel::remove(int id)
{
    const ContentItem *item = announcement(id);
    if (!item)
        return;
    if (QMessageBox::question(this, tr("Удаление"), tr("Удалить объявление «%1»?").arg(item->title)) != QMessageBox::Yes)
        return;
    if (!m_repository->remove(id)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить запись."));
        return;
    }
    if (m_editor->itemId() == id) {
        ContentItem draft;
        draft.type = ContentType::Announcement;
        m_editor->setItem(draft);
    }
    if (m_selectedId == id)
        m_selectedId = -1;
    reload();
    emit libraryChanged();
}

void AnnouncementsPanel::save(ContentItem item)
{
    // A newly chosen background is copied into Sermon's own folder, so the
    // announcement keeps working if the original file moves.
    if (item.backgroundType != BackgroundType::None) {
        const QFileInfo source(item.backgroundPath);
        if (source.absolutePath() != QFileInfo(Database::backgroundsDir()).absoluteFilePath()) {
            const QString dest = Database::backgroundsDir() + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces)
                + QLatin1Char('.') + source.suffix();
            if (!QFile::copy(item.backgroundPath, dest)) {
                QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить фон."));
                return;
            }
            item.backgroundPath = dest;
        }
    }
    const bool ok = item.id < 0 ? m_repository->add(item) : m_repository->update(item);
    if (!ok) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        return;
    }
    m_selectedId = item.id;
    m_editor->setItem(item);
    reload();
    emit announcementSaved(item);
    emit libraryChanged();
}
