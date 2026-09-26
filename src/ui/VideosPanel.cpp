#include "VideosPanel.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"
#include "TimerWidgets.h"
#include "VideoThumbnailer.h"
#include "core/ContentRepository.h"
#include "core/Database.h"
#include "display/SlideContentBuilder.h"
#include "display/SlideRenderWidget.h"

#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace TimerUi;

namespace {

constexpr double ThumbRatio = 165.0 / 226.0; // design.pen video "Thumb"
constexpr int TitleHeight = 30;

QStringList videoExtensions()
{
    return {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("mkv"),
            QStringLiteral("webm"), QStringLiteral("m4v")};
}

bool isRemote(const ContentItem &item)
{
    return item.imagePath.startsWith(QStringLiteral("http://")) || item.imagePath.startsWith(QStringLiteral("https://"));
}

QString formatLabel(const ContentItem &item)
{
    return isRemote(item) ? QStringLiteral("YouTube") : QFileInfo(item.imagePath).suffix().toLower();
}

QString durationLabel(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 seconds = ms / 1000;
    const QString mmss = QStringLiteral("%1:%2").arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return seconds >= 3600 ? QStringLiteral("%1:%2").arg(seconds / 3600).arg(mmss) : mmss;
}

QString fileSizeText(qint64 bytes)
{
    if (bytes >= 1024 * 1024 * 1024)
        return QObject::tr("%1 ГБ").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1));
    if (bytes >= 1024 * 1024)
        return QObject::tr("%1 МБ").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QObject::tr("%1 КБ").arg(qMax<qint64>(1, bytes / 1024));
}

void drawPoster(QPainter &painter, const QRectF &bounds, const ContentItem &item, qreal radius)
{
    QPainterPath clip;
    clip.addRoundedRect(bounds, Theme::radius(radius), Theme::radius(radius));
    painter.save();
    painter.setClipPath(clip);
    const QPixmap poster = isRemote(item) ? QPixmap() : VideoThumbnailer::instance()->poster(item.imagePath);
    if (poster.isNull()) {
        painter.fillRect(bounds, QColor(0x1b, 0x23, 0x33));
        const int size = bounds.height() > 80 ? 30 : 20;
        painter.drawPixmap(QPointF(bounds.center().x() - size / 2.0, bounds.center().y() - size / 2.0),
                           IconProvider::pixmap(isRemote(item) ? QStringLiteral("youtube") : QStringLiteral("video"),
                                                QColor(0x8b, 0x93, 0xa8), size));
    } else {
        const QSizeF source = poster.size();
        const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
        const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
        painter.drawPixmap(bounds, poster,
                           QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
    }
    painter.restore();
}

// A dark rounded badge with white text (duration / format).
void drawBadge(QPainter &painter, const QPointF &anchor, bool alignRight, const QString &label, int alpha, qreal fontSize)
{
    QFont font = painter.font();
    font.setPixelSize(qRound(fontSize));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const qreal width = painter.fontMetrics().horizontalAdvance(label) + 14;
    const qreal height = fontSize + 10;
    const QRectF rect(alignRight ? anchor.x() - width : anchor.x(), anchor.y() - height, width, height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, alpha));
    painter.drawRoundedRect(rect, Theme::radius(6), Theme::radius(6));
    painter.setPen(Qt::white);
    painter.drawText(rect, Qt::AlignCenter, label);
}

ClickFrame *outlineButton(const QString &icon, const QString &label)
{
    auto *button = new ClickFrame;
    button->setFixedHeight(48);
    styleFrame(button, QStringLiteral("VideoOutline"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 10px;").arg(Theme::BorderLight));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(label.isEmpty() ? 0 : 20, 0, label.isEmpty() ? 0 : 20, 0);
    layout->setSpacing(9);
    if (label.isEmpty())
        button->setFixedWidth(48);
    layout->addWidget(TimerUi::icon(icon, Theme::TextDarkPrimary, 18), 0, Qt::AlignCenter);
    if (!label.isEmpty())
        layout->addWidget(text(label, 15, 600, Theme::TextDarkPrimary));
    return button;
}

// Blue "label | ⌄" split button (design.pen "Add Video Button" / "On Screen Split Button").
ClickFrame *splitButton(const QString &icon, const QString &label, int height, QLabel **labelOut)
{
    auto *button = new ClickFrame;
    button->setFixedHeight(height);
    styleFrame(button, QStringLiteral("VideoSplit"),
               QStringLiteral("background: %1; border: none; border-radius: 10px;").arg(Theme::AccentBlue));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(18, 0, 14, 0);
    layout->setSpacing(9);
    layout->addWidget(TimerUi::icon(icon, Theme::TextLightPrimary, 17));
    auto *labelWidget = text(label, 15, 600, Theme::TextLightPrimary);
    layout->addWidget(labelWidget);
    auto *divider = new QFrame;
    divider->setFixedSize(1, 24);
    divider->setStyleSheet(QStringLiteral("background: rgba(255,255,255,77); border: none;"));
    layout->addSpacing(4);
    layout->addWidget(divider);
    layout->addSpacing(4);
    auto *chevron = TimerUi::icon(QStringLiteral("chevron-down"), Theme::TextLightPrimary, 15);
    chevron->setObjectName(QStringLiteral("SplitChevron"));
    layout->addWidget(chevron);
    if (labelOut)
        *labelOut = labelWidget;
    return button;
}

// True when the click landed on the split button's chevron part.
bool onChevron(ClickFrame *button)
{
    const QWidget *chevron = button->findChild<QWidget *>(QStringLiteral("SplitChevron"));
    return chevron && button->mapFromGlobal(QCursor::pos()).x() >= chevron->geometry().left() - 12;
}

class VideoItemWidget : public QWidget {
public:
    explicit VideoItemWidget(const ContentItem &item)
        : m_item(item)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(item.title);
    }

    void setSelected(bool selected)
    {
        if (m_selected != selected) {
            m_selected = selected;
            update();
        }
    }

    std::function<void(Qt::KeyboardModifiers)> onClick;
    std::function<void()> onDoubleClick;
    std::function<void(const QPoint &)> onMenu;

protected:
    virtual QRectF menuRect() const = 0;

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (menuRect().adjusted(-4, -4, 4, 4).contains(event->position())) {
            if (onClick && !m_selected)
                onClick(Qt::NoModifier);
            if (onMenu)
                onMenu(mapToGlobal(menuRect().bottomLeft().toPoint() + QPoint(0, 6)));
            return;
        }
        if (onClick)
            onClick(event->modifiers());
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !menuRect().contains(event->position()) && onDoubleClick)
            onDoubleClick();
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onClick && !m_selected)
            onClick(Qt::NoModifier);
        if (onMenu)
            onMenu(event->globalPos());
    }

    ContentItem m_item;
    bool m_selected = false;
};

// Grid tile: poster with duration / format badges and "⋯", title below.
class VideoCard : public VideoItemWidget {
public:
    using VideoItemWidget::VideoItemWidget;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qRound(width * ThumbRatio) + TitleHeight; }
    QSize sizeHint() const override { return QSize(226, 165 + TitleHeight); }

protected:
    QRectF thumbRect() const { return QRectF(0, 0, width(), height() - TitleHeight); }
    QRectF menuRect() const override { return QRectF(width() - 40, 10, 30, 30); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF thumb = thumbRect();
        drawPoster(painter, thumb, m_item, 10);
        if (underMouse()) {
            QPainterPath clip;
            clip.addRoundedRect(thumb, Theme::radius(10), Theme::radius(10));
            painter.fillPath(clip, QColor(255, 255, 255, 18));
        }

        // "⋯" (design.pen "More Button": 30px dark circle).
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 115));
        painter.drawEllipse(menuRect());
        painter.drawPixmap(QPointF(menuRect().center().x() - 8, menuRect().center().y() - 8),
                           IconProvider::pixmap(QStringLiteral("ellipsis"), Qt::white, 16));

        const qint64 duration = isRemote(m_item) ? -1 : VideoThumbnailer::instance()->duration(m_item.imagePath);
        drawBadge(painter, QPointF(8, thumb.bottom() - 8), false, durationLabel(duration), 166, 12.5);
        drawBadge(painter, QPointF(thumb.right() - 8, thumb.bottom() - 8), true, formatLabel(m_item), 102, 11.5);

        if (m_selected) {
            painter.setPen(QPen(QColor(Theme::AccentBlue), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(thumb.adjusted(1, 1, -1, -1), Theme::radius(9), Theme::radius(9));
        }

        QFont font = painter.font();
        font.setPixelSize(15);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(Theme::TextDarkPrimary));
        const QRectF titleRect(2, thumb.bottom() + 6, width() - 4, TitleHeight - 6);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_item.title, Qt::ElideRight, int(titleRect.width())));
    }
};

// List row: poster, title, duration · format · size, date, "⋯".
class VideoRow : public VideoItemWidget {
public:
    explicit VideoRow(const ContentItem &item)
        : VideoItemWidget(item)
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
        drawPoster(painter, thumb, m_item, 7);
        const qint64 duration = isRemote(m_item) ? -1 : VideoThumbnailer::instance()->duration(m_item.imagePath);
        drawBadge(painter, QPointF(thumb.left() + 4, thumb.bottom() - 4), false, durationLabel(duration), 166, 10);

        QFont font = painter.font();
        font.setPixelSize(14);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(Theme::TextDarkPrimary));
        const qreal textLeft = 116;
        const qreal textWidth = width() - textLeft - 170;
        painter.drawText(QRectF(textLeft, 14, textWidth, 22), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_item.title, Qt::ElideRight, int(textWidth)));
        QStringList meta{durationLabel(duration), formatLabel(m_item)};
        if (!isRemote(m_item))
            meta << fileSizeText(QFileInfo(m_item.imagePath).size());
        font.setPixelSize(12);
        font.setWeight(QFont::Medium);
        painter.setFont(font);
        painter.setPen(QColor(Theme::TextDarkSecondary));
        painter.drawText(QRectF(textLeft, 38, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, meta.join(QStringLiteral(" · ")));
        painter.drawText(QRectF(width() - 160, 0, 110, height()), Qt::AlignRight | Qt::AlignVCenter,
                         m_item.createdAt.toString(QStringLiteral("dd.MM.yyyy")));
        painter.drawPixmap(QPointF(menuRect().center().x() - 9, menuRect().center().y() - 9),
                           IconProvider::pixmap(QStringLiteral("ellipsis"), QColor(Theme::TextDarkSecondary), 18));
    }
};

} // namespace

// ---------------------------------------------------------------------------

VideosPanel::VideosPanel(ContentRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_repository(repository)
{
    setObjectName(QStringLiteral("VideosPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#VideosPanel { background: #ffffff; }"));
    setAcceptDrops(true);
    m_listView = QSettings().value(QStringLiteral("videos/listView"), false).toBool();

    // design.pen "Video Content": vertical, gap 16, padding [22, 28, 0, 28].
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 22, 28, 0);
    layout->setSpacing(16);
    layout->addWidget(text(tr("Видео"), 28, 800, Theme::TextDarkPrimary));
    layout->addWidget(buildToolbar());

    m_stack = new QStackedWidget;
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    m_items = new QWidget;
    m_items->setObjectName(QStringLiteral("VideoItems"));
    m_items->setStyleSheet(QStringLiteral("QWidget#VideoItems { background: #ffffff; }"));
    m_scroll->setWidget(m_items);
    m_stack->addWidget(m_scroll);

    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(10);
    auto *emptyIcon = new QLabel;
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(IconProvider::pixmap(QStringLiteral("video"), QColor(Theme::Placeholder), 44));
    emptyLayout->addWidget(emptyIcon);
    m_emptyTitle = text(QString(), 17, 700, Theme::TextDarkPrimary);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyHint = text(QString(), 13.5, 500, Theme::TextDarkSecondary);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    emptyLayout->addWidget(m_emptyHint);
    m_stack->addWidget(empty);
    layout->addWidget(m_stack, 1);
    layout->addWidget(buildSelectionBar());

    const auto shortcut = [this](const QKeySequence &keys, const std::function<void()> &action) {
        auto *s = new QShortcut(keys, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_Escape), [this]() {
        m_selection.clear();
        refreshSelection();
    });
    shortcut(QKeySequence::Delete, [this]() { deleteSelection(); });
    shortcut(QKeySequence(Qt::Key_Return), [this]() { goLiveSelected(); });
    shortcut(QKeySequence(Qt::Key_Enter), [this]() { goLiveSelected(); });

    connect(VideoThumbnailer::instance(), &VideoThumbnailer::ready, this, [this]() {
        for (QWidget *widget : std::as_const(m_itemWidgets))
            widget->update();
    });

    setListView(m_listView);
    reload();
}

QWidget *VideosPanel::buildToolbar()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    m_searchEdit->setPlaceholderText(tr("Поиск по названию..."));
    m_searchEdit->setFixedHeight(44);
    m_searchEdit->setMaximumWidth(440);
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

    QLabel *addLabel = nullptr;
    m_addButton = splitButton(QStringLiteral("plus"), tr("Добавить видео"), 44, &addLabel);
    m_addButton->setToolTip(tr("Добавить видео с компьютера (стрелка — по ссылке YouTube). Файлы можно и перетащить в окно."));
    m_addButton->onClick = [this]() {
        if (onChevron(m_addButton)) {
            showAddMenu(m_addButton);
            return;
        }
        const QString filter = tr("Видео (*.%1)").arg(videoExtensions().join(QStringLiteral(" *.")));
        importFiles(QFileDialog::getOpenFileNames(this, tr("Добавить видео"), QString(), filter));
    };
    layout->addWidget(m_addButton);
    // Narrow window: the add button drops its label, keeping "+ ⌄".
    struct LabelCollapser : QObject {
        QWidget *row;
        QWidget *label;
        LabelCollapser(QWidget *r, QWidget *l) : QObject(r), row(r), label(l) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                label->setVisible(row->width() >= 680);
            return false;
        }
    };
    row->installEventFilter(new LabelCollapser(row, addLabel));

    auto *toggle = new QFrame;
    toggle->setFixedHeight(44);
    styleFrame(toggle, QStringLiteral("VideoViewToggle"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
    auto *toggleLayout = new QHBoxLayout(toggle);
    toggleLayout->setContentsMargins(1, 1, 1, 1);
    toggleLayout->setSpacing(0);
    m_gridButton = new ClickFrame;
    m_listButton = new ClickFrame;
    m_gridButton->setToolTip(tr("Плитка"));
    m_listButton->setToolTip(tr("Список"));
    for (ClickFrame *button : {m_gridButton, m_listButton}) {
        button->setFixedSize(45, 42);
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
    m_sortField->setFixedHeight(44);
    m_sortField->setMinimumWidth(150);
    m_sortField->onSelected = [this](int index) {
        m_sort = index;
        rebuildItems();
    };
    layout->addWidget(m_sortField);
    return row;
}

QWidget *VideosPanel::buildSelectionBar()
{
    // design.pen "Action Bar": light grey band, rounded top corners.
    m_selectionBar = new QFrame;
    styleFrame(m_selectionBar, QStringLiteral("VideoActionBar"),
               QStringLiteral("background: #f4f5f7; border: none; border-top-left-radius: 12px; border-top-right-radius: 12px;"));
    auto *flow = new FlowLayout(FlowLayout::Mode::Natural, 12, 0, m_selectionBar);
    flow->setContentsMargins(16, 14, 16, 14);
    flow->setSpaceBetween(true);
    flow->setCenterItems(true);
    m_selectionLabel = text(QString(), 14.5, 500, Theme::TextDarkPrimary);
    flow->addWidget(m_selectionLabel);

    auto *buttons = new QWidget;
    auto *layout = new QHBoxLayout(buttons);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *remove = outlineButton(QStringLiteral("trash-2"), QString());
    remove->setToolTip(tr("Удалить"));
    remove->onClick = [this]() { deleteSelection(); };
    auto *previewButton = outlineButton(QStringLiteral("eye"), tr("Предпросмотр"));
    previewButton->onClick = [this]() {
        const QList<ContentItem> selected = selectedItems();
        if (!selected.isEmpty())
            preview(selected.first());
    };
    auto *onScreen = splitButton(QStringLiteral("monitor"), tr("На экран"), 48, nullptr);
    onScreen->setToolTip(tr("Показать на проекторе (стрелка — показать фоном в цикле)"));
    onScreen->onClick = [this, onScreen]() {
        if (onChevron(onScreen))
            showOnScreenMenu(onScreen);
        else
            goLiveSelected(false);
    };
    layout->addWidget(remove);
    layout->addWidget(previewButton);
    layout->addWidget(onScreen);
    flow->addWidget(buttons);
    m_selectionBar->hide();
    return m_selectionBar;
}

void VideosPanel::setListView(bool list)
{
    m_listView = list;
    QSettings().setValue(QStringLiteral("videos/listView"), list);
    const auto paint = [](ClickFrame *button, bool on, const QString &iconName, bool leftEdge) {
        button->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; border-top-%2-radius: 8px; border-bottom-%2-radius: 8px; }")
                                  .arg(on ? Theme::AccentBlueBg : QStringLiteral("#ffffff"),
                                       leftEdge ? QStringLiteral("left") : QStringLiteral("right")));
        button->findChild<QLabel *>(QStringLiteral("ToggleIcon"))
            ->setPixmap(IconProvider::pixmap(iconName, QColor(on ? Theme::AccentBlue : Theme::TextDarkSecondary), 18));
    };
    paint(m_gridButton, !list, QStringLiteral("layout-grid"), true);
    paint(m_listButton, list, QStringLiteral("list"), false);

    if (QLayout *old = m_items->layout()) {
        while (QLayoutItem *item = old->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        delete old;
    }
    m_itemWidgets.clear();
    if (list) {
        auto *column = new QVBoxLayout(m_items);
        column->setContentsMargins(0, 0, 0, 16);
        column->setSpacing(8);
        column->setAlignment(Qt::AlignTop);
    } else {
        auto *flow = new FlowLayout(FlowLayout::Mode::EqualColumns, 14, 160, m_items);
        flow->setContentsMargins(0, 0, 0, 16);
        flow->setColumnsFromWidthOnly(true);
    }
    rebuildItems();
}

// ---- Data ------------------------------------------------------------------

void VideosPanel::reload()
{
    m_videos = m_repository->fetch(QString(), ContentType::Video, SortOrder::DateAddedDesc);
    QSet<int> existing;
    for (const ContentItem &item : std::as_const(m_videos))
        existing.insert(item.id);
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [&](int id) { return !existing.contains(id); }),
                      m_selection.end());
    rebuildItems();
}

const ContentItem *VideosPanel::video(int id) const
{
    for (const ContentItem &item : m_videos) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

QList<ContentItem> VideosPanel::visibleVideos() const
{
    QList<ContentItem> result;
    for (const ContentItem &item : m_videos) {
        if (m_search.isEmpty() || item.title.contains(m_search, Qt::CaseInsensitive))
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

QList<ContentItem> VideosPanel::selectedItems() const
{
    QList<ContentItem> result;
    for (int id : m_shownIds) {
        if (m_selection.contains(id)) {
            if (const ContentItem *item = video(id))
                result << *item;
        }
    }
    return result;
}

void VideosPanel::rebuildItems()
{
    QLayout *layout = m_items->layout();
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_itemWidgets.clear();
    m_shownIds.clear();

    const QList<ContentItem> videos = visibleVideos();
    for (const ContentItem &item : videos) {
        VideoItemWidget *widget = m_listView ? static_cast<VideoItemWidget *>(new VideoRow(item))
                                             : static_cast<VideoItemWidget *>(new VideoCard(item));
        if (!m_listView) {
            QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            widget->setSizePolicy(policy);
        }
        const int id = item.id;
        widget->onClick = [this, id](Qt::KeyboardModifiers modifiers) { clickItem(id, modifiers); };
        widget->onDoubleClick = [this, id]() {
            if (const ContentItem *item = video(id))
                emit goLiveRequested(*item, false);
        };
        widget->onMenu = [this, id](const QPoint &pos) { showItemMenu(id, pos); };
        layout->addWidget(widget);
        m_itemWidgets.insert(id, widget);
        m_shownIds << id;
    }

    if (videos.isEmpty()) {
        m_emptyTitle->setText(m_videos.isEmpty() ? tr("Пока нет видео") : tr("Ничего не найдено"));
        m_emptyHint->setText(m_videos.isEmpty() ? tr("Нажмите «Добавить видео» или перетащите видеофайлы в это окно.")
                                                : tr("Попробуйте другой поисковый запрос."));
        m_stack->setCurrentIndex(1);
    } else {
        m_stack->setCurrentIndex(0);
    }
    refreshSelection();
    m_items->updateGeometry();
}

void VideosPanel::refreshSelection()
{
    for (auto it = m_itemWidgets.constBegin(); it != m_itemWidgets.constEnd(); ++it)
        static_cast<VideoItemWidget *>(it.value())->setSelected(m_selection.contains(it.key()));
    const int count = selectedItems().size();
    m_selectionBar->setVisible(count > 0);
    m_selectionLabel->setText(tr("Выбрано: %1 видео").arg(count));
}

// ---- Interaction -----------------------------------------------------------

void VideosPanel::clickItem(int id, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ShiftModifier && m_anchorId >= 0) {
        const int from = m_shownIds.indexOf(m_anchorId);
        const int to = m_shownIds.indexOf(id);
        if (from >= 0 && to >= 0) {
            m_selection.clear();
            for (int i = qMin(from, to); i <= qMax(from, to); ++i)
                m_selection << m_shownIds.at(i);
        }
    } else if (modifiers & Qt::ControlModifier) {
        if (m_selection.contains(id))
            m_selection.removeAll(id);
        else
            m_selection << id;
        m_anchorId = id;
    } else {
        m_selection = {id};
        m_anchorId = id;
    }
    refreshSelection();
}

void VideosPanel::goLiveSelected(bool loop)
{
    const QList<ContentItem> selected = selectedItems();
    if (!selected.isEmpty())
        emit goLiveRequested(selected.first(), loop);
}

void VideosPanel::showItemMenu(int id, const QPoint &globalPos)
{
    const ContentItem *item = video(id);
    if (!item)
        return;
    const ContentItem copy = *item;
    QMenu menu(this);
    menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16), tr("На экран"), this,
                   [this, copy]() { emit goLiveRequested(copy, false); });
    menu.addAction(IconProvider::icon(QStringLiteral("repeat"), QColor(Theme::TextDarkPrimary), 16),
                   tr("Как фон (в цикле, без звука)"), this, [this, copy]() { emit goLiveRequested(copy, true); });
    menu.addAction(IconProvider::icon(QStringLiteral("eye"), QColor(Theme::TextDarkPrimary), 16), tr("Предпросмотр"), this,
                   [this, copy]() { preview(copy); });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("pencil"), QColor(Theme::TextDarkPrimary), 16), tr("Переименовать…"), this,
                   [this, id]() { renameVideo(id); });
    const int count = m_selection.size();
    menu.addAction(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 16),
                   count > 1 ? tr("Удалить выбранные (%1)").arg(count) : tr("Удалить"), this, &VideosPanel::deleteSelection);
    menu.exec(globalPos);
}

void VideosPanel::showAddMenu(QWidget *anchor)
{
    QMenu menu(this);
    menu.addAction(IconProvider::icon(QStringLiteral("upload"), QColor(Theme::TextDarkPrimary), 16), tr("С компьютера…"), this,
                   [this]() {
                       const QString filter = tr("Видео (*.%1)").arg(videoExtensions().join(QStringLiteral(" *.")));
                       importFiles(QFileDialog::getOpenFileNames(this, tr("Добавить видео"), QString(), filter));
                   });
    menu.addAction(IconProvider::icon(QStringLiteral("youtube"), QColor(Theme::TextDarkPrimary), 16), tr("По ссылке (YouTube)…"),
                   this, &VideosPanel::importFromLink);
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
}

void VideosPanel::showOnScreenMenu(QWidget *anchor)
{
    QMenu menu(this);
    menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16), tr("На экран (со звуком)"),
                   this, [this]() { goLiveSelected(false); });
    menu.addAction(IconProvider::icon(QStringLiteral("repeat"), QColor(Theme::TextDarkPrimary), 16),
                   tr("Как фон (в цикле, без звука)"), this, [this]() { goLiveSelected(true); });
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
}

void VideosPanel::preview(const ContentItem &item)
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Предпросмотр — %1").arg(item.title));
    dialog->resize(960, 540);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *render = new SlideRenderWidget(dialog);
    render->setContent(buildSlideContent(item, QStringList{QString()}, 0, false));
    layout->addWidget(render);
    dialog->show();
}

void VideosPanel::renameVideo(int id)
{
    const ContentItem *item = video(id);
    if (!item)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Видео"), tr("Название:"), QLineEdit::Normal, item->title, &ok).trimmed();
    if (!ok || name.isEmpty() || name == item->title)
        return;
    ContentItem updated = *item;
    updated.title = name;
    m_repository->update(updated);
    reload();
}

void VideosPanel::deleteSelection()
{
    const QList<ContentItem> selected = selectedItems();
    if (selected.isEmpty())
        return;
    const QString question = selected.size() == 1 ? tr("Удалить видео «%1» из библиотеки?").arg(selected.first().title)
                                                   : tr("Удалить %1 видео из библиотеки?").arg(selected.size());
    if (QMessageBox::question(this, tr("Удалить"), question) != QMessageBox::Yes)
        return;
    const QString ownDir = QFileInfo(Database::videosDir()).absoluteFilePath();
    for (const ContentItem &item : selected) {
        m_repository->remove(item.id);
        // Only files Sermon copied/downloaded into its own library folder.
        if (!isRemote(item) && QFileInfo(item.imagePath).absolutePath() == ownDir)
            QFile::remove(item.imagePath);
    }
    m_selection.clear();
    reload();
    emit libraryChanged();
}

// ---- Import ----------------------------------------------------------------

void VideosPanel::addItem(ContentItem item)
{
    item.type = ContentType::Video;
    if (!m_repository->add(item)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить видео."));
        return;
    }
    m_selection = {item.id};
    m_anchorId = item.id;
    reload();
    emit libraryChanged();
}

void VideosPanel::importFiles(const QStringList &paths)
{
    QStringList failed;
    QList<int> added;
    for (const QString &source : paths) {
        const QFileInfo info(source);
        if (!videoExtensions().contains(info.suffix().toLower())) {
            failed << info.fileName();
            continue;
        }
        // Same as before: copy into Sermon's own videos folder.
        const QString dest = Database::videosDir() + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QLatin1Char('.') + info.suffix();
        if (!QFile::copy(source, dest)) {
            failed << info.fileName();
            continue;
        }
        ContentItem item;
        item.type = ContentType::Video;
        item.title = info.completeBaseName();
        item.imagePath = dest;
        if (!m_repository->add(item)) {
            QFile::remove(dest);
            failed << info.fileName();
            continue;
        }
        added << item.id;
    }
    if (!added.isEmpty()) {
        m_selection = added;
        m_anchorId = added.last();
        reload();
        emit libraryChanged();
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Добавить видео"), tr("Не удалось добавить:\n%1").arg(failed.join(QLatin1Char('\n'))));
}

void VideosPanel::importFromLink()
{
    if (m_downloading) {
        QMessageBox::information(this, tr("Видео по ссылке"), tr("Предыдущее видео ещё скачивается."));
        return;
    }
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Видео по ссылке"), tr("Ссылка на видео YouTube:"), QLineEdit::Normal,
                                              QString(), &ok).trimmed();
    if (!ok || url.isEmpty())
        return;

    const QString ytDlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (ytDlp.isEmpty()) {
        const auto answer = QMessageBox::question(this, tr("Нужна программа yt-dlp"),
            tr("Чтобы скачать видео с YouTube, нужна бесплатная утилита yt-dlp (она не входит в Sermon):\n"
               "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe — положите её в C:\\Windows "
               "или в папку с Sermon.exe.\n\n"
               "Добавить ссылку без скачивания? Тогда видео будет показываться только в OBS (через браузер), "
               "а не в окне проектора."));
        if (answer == QMessageBox::Yes) {
            ContentItem item;
            item.title = tr("Видео YouTube");
            item.imagePath = url;
            addItem(item);
        }
        return;
    }

    const QString destDir = Database::videosDir();
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_downloading = true;
    m_addButton->setEnabled(false);
    m_addButton->setToolTip(tr("Скачивание видео…"));
    setCursor(Qt::BusyCursor);

    auto *process = new QProcess(this);
    const QStringList args{QStringLiteral("-f"), QStringLiteral("bv*[ext=mp4]+ba[ext=m4a]/best[ext=mp4]/best"),
                           QStringLiteral("-o"), destDir + QLatin1Char('/') + uuid + QStringLiteral(".%(ext)s"),
                           QStringLiteral("--print"), QStringLiteral("%(title)s"), url};
    connect(process, &QProcess::finished, this, [this, process, destDir, uuid](int exitCode, QProcess::ExitStatus) {
        const QString title = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        process->deleteLater();
        m_downloading = false;
        m_addButton->setEnabled(true);
        unsetCursor();
        const QDir dir(destDir);
        const QStringList matches = dir.entryList(QStringList{uuid + QStringLiteral("*")}, QDir::Files);
        if (exitCode != 0 || matches.isEmpty()) {
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось скачать видео. Проверьте ссылку и подключение к интернету."));
            return;
        }
        ContentItem item;
        item.title = title.isEmpty() ? QFileInfo(matches.first()).completeBaseName() : title;
        item.imagePath = dir.absoluteFilePath(matches.first());
        addItem(item);
    });
    process->start(ytDlp, args);
}

void VideosPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void VideosPanel::dropEvent(QDropEvent *event)
{
    QStringList files;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            files << url.toLocalFile();
    }
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        importFiles(files);
    }
}
