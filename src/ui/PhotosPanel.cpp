#include "PhotosPanel.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "TimerWidgets.h"
#include "core/ContentRepository.h"
#include "core/Database.h"
#include "display/SlideContentBuilder.h"
#include "display/SlideRenderWidget.h"

#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>

using namespace TimerUi;

namespace {

constexpr double CardRatio = 170.0 / 227.0; // design.pen "Photo 01.jpg" card

QStringList imageExtensions()
{
    return {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"),
            QStringLiteral("webp"), QStringLiteral("gif")};
}

QString fileSizeText(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QObject::tr("%1 МБ").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QObject::tr("%1 КБ").arg(qMax<qint64>(1, bytes / 1024));
}

QString displayName(const ContentItem &item)
{
    const QString suffix = QFileInfo(item.imagePath).suffix();
    return suffix.isEmpty() ? item.title : QStringLiteral("%1.%2").arg(item.title, suffix);
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

void drawThumb(QPainter &painter, const QRectF &bounds, const QString &path, qreal radius)
{
    QPainterPath clip;
    clip.addRoundedRect(bounds, Theme::radius(radius), Theme::radius(radius));
    painter.save();
    painter.setClipPath(clip);
    const QPixmap pixmap = ThumbnailCache::instance()->thumbnail(path);
    if (pixmap.isNull()) {
        painter.fillRect(bounds, QColor(Theme::SurfaceMuted));
        const int size = bounds.height() > 80 ? 26 : 18;
        painter.drawPixmap(QPointF(bounds.center().x() - size / 2.0, bounds.center().y() - size / 2.0),
                           IconProvider::pixmap(QStringLiteral("image"), QColor(Theme::Placeholder), size));
    } else {
        drawCover(painter, bounds, pixmap);
    }
    painter.restore();
}

// design.pen "Add Photo Button" (46px tall, 15px label).
ClickFrame *primaryButton(const QString &icon, const QString &label)
{
    auto *button = new ClickFrame;
    button->setFixedHeight(46);
    styleFrame(button, QStringLiteral("PhotoAdd"),
               QStringLiteral("background: %1; border: none; border-radius: 9px;").arg(Theme::AccentBlue));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(9);
    layout->addWidget(TimerUi::icon(icon, Theme::TextLightPrimary, 17));
    auto *labelWidget = text(label, 15, 600, Theme::TextLightPrimary);
    labelWidget->setObjectName(QStringLiteral("ButtonLabel"));
    layout->addWidget(labelWidget);
    return button;
}

// Shared behaviour of the grid card and the list row.
class PhotoItemWidget : public QWidget {
public:
    PhotoItemWidget(const ContentItem &item)
        : m_item(item)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(displayName(item));
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
    // Where the "⋯" button is, in widget coordinates.
    virtual QRectF menuRect() const = 0;

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (menuRect().adjusted(-6, -6, 6, 6).contains(event->position())) {
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
        if (event->button() == Qt::LeftButton && !menuRect().adjusted(-6, -6, 6, 6).contains(event->position()) && onDoubleClick)
            onDoubleClick();
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onClick && !m_selected)
            onClick(Qt::NoModifier);
        if (onMenu)
            onMenu(event->globalPos());
    }

    void drawDots(QPainter &painter, const QRectF &rect, const QColor &color) const
    {
        painter.drawPixmap(QPointF(rect.center().x() - 9, rect.center().y() - 9),
                           IconProvider::pixmap(QStringLiteral("ellipsis"), color, 18));
    }

    ContentItem m_item;
    bool m_selected = false;
};

// Grid tile: photo, name + "⋯" over a bottom shade, blue outline when selected.
class PhotoCard : public PhotoItemWidget {
public:
    using PhotoItemWidget::PhotoItemWidget;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qRound(width * CardRatio); }
    QSize sizeHint() const override { return QSize(227, 170); }

protected:
    QRectF menuRect() const override { return QRectF(width() - 36, height() - 34, 26, 26); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = QRectF(rect());
        drawThumb(painter, bounds, m_item.imagePath, 10);

        QPainterPath clip;
        clip.addRoundedRect(bounds, Theme::radius(10), Theme::radius(10));
        painter.save();
        painter.setClipPath(clip);
        // design.pen "Name Bar": 60px shade from transparent to 65% black.
        const QRectF bar(0, bounds.height() - 60, bounds.width(), 60);
        QLinearGradient shade(bar.topLeft(), bar.bottomLeft());
        shade.setColorAt(0, QColor(0, 0, 0, 0));
        shade.setColorAt(1, QColor(0, 0, 0, 166));
        painter.fillRect(bar, shade);
        if (underMouse())
            painter.fillRect(bounds, QColor(255, 255, 255, 18));
        painter.restore();

        QFont font = painter.font();
        font.setPixelSize(14);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(Qt::white);
        const QRectF nameRect(12, bounds.height() - 34, bounds.width() - 56, 24);
        painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(displayName(m_item), Qt::ElideRight, int(nameRect.width())));
        drawDots(painter, menuRect(), Qt::white);

        if (m_selected) {
            painter.setPen(QPen(QColor(Theme::AccentBlue), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), Theme::radius(9), Theme::radius(9));
        }
    }
};

// List row: thumbnail, name, resolution · size, date, "⋯".
class PhotoRow : public PhotoItemWidget {
public:
    PhotoRow(const ContentItem &item)
        : PhotoItemWidget(item)
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

        drawThumb(painter, QRectF(10, 8, 90, 56), m_item.imagePath, 7);

        QFont font = painter.font();
        font.setPixelSize(14);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(Theme::TextDarkPrimary));
        const qreal textLeft = 116;
        const qreal textWidth = width() - textLeft - 170;
        painter.drawText(QRectF(textLeft, 14, textWidth, 22), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(displayName(m_item), Qt::ElideRight, int(textWidth)));

        const QSize size = ThumbnailCache::instance()->imageSize(m_item.imagePath);
        QStringList meta;
        if (size.isValid())
            meta << QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
        meta << fileSizeText(QFileInfo(m_item.imagePath).size());
        font.setPixelSize(12);
        font.setWeight(QFont::Medium);
        painter.setFont(font);
        painter.setPen(QColor(Theme::TextDarkSecondary));
        painter.drawText(QRectF(textLeft, 38, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, meta.join(QStringLiteral(" · ")));
        painter.drawText(QRectF(width() - 160, 0, 110, height()), Qt::AlignRight | Qt::AlignVCenter,
                         m_item.createdAt.toString(QStringLiteral("dd.MM.yyyy")));
        drawDots(painter, menuRect(), QColor(Theme::TextDarkSecondary));
    }
};

} // namespace

// ---------------------------------------------------------------------------

PhotosPanel::PhotosPanel(ContentRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_repository(repository)
{
    setObjectName(QStringLiteral("PhotosPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#PhotosPanel { background: #ffffff; }"));
    setAcceptDrops(true);
    m_listView = QSettings().value(QStringLiteral("photos/listView"), false).toBool();

    // design.pen "Photos Content": vertical, gap 22, padding [26, 28, 0, 28].
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 0);
    layout->setSpacing(22);
    layout->addWidget(text(tr("Фото"), 28, 800, Theme::TextDarkPrimary));
    layout->addWidget(buildToolbar());

    m_stack = new QStackedWidget;
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    m_items = new QWidget;
    m_items->setObjectName(QStringLiteral("PhotoItems"));
    m_items->setStyleSheet(QStringLiteral("QWidget#PhotoItems { background: #ffffff; }"));
    m_scroll->setWidget(m_items);
    m_stack->addWidget(m_scroll);

    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(10);
    auto *emptyIcon = new QLabel;
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(IconProvider::pixmap(QStringLiteral("image-plus"), QColor(Theme::Placeholder), 44));
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

    const auto shortcut = [this](const QKeySequence &keys, const std::function<void()> &action) {
        auto *s = new QShortcut(keys, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence::SelectAll, [this]() {
        m_selection = m_shownIds;
        refreshSelection();
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this]() {
        m_selection.clear();
        refreshSelection();
    });
    shortcut(QKeySequence::Delete, [this]() { deleteSelection(); });
    shortcut(QKeySequence(Qt::Key_Return), [this]() { showSelected(); });
    shortcut(QKeySequence(Qt::Key_Enter), [this]() { showSelected(); });

    connect(ThumbnailCache::instance(), &ThumbnailCache::ready, this, [this]() {
        for (QWidget *widget : std::as_const(m_itemWidgets))
            widget->update();
    });

    setListView(m_listView);
    reload();
}

QWidget *PhotosPanel::buildToolbar()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    m_searchEdit->setPlaceholderText(tr("Поиск по названию..."));
    m_searchEdit->setFixedHeight(46);
    m_searchEdit->setMaximumWidth(380);
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
    layout->addStretch();

    auto *add = primaryButton(QStringLiteral("plus"), tr("Добавить фото"));
    // On a narrow window the toolbar can't fit search + labelled button +
    // toggle + sort: the button drops to its "+" icon (tooltip keeps the text).
    struct LabelCollapser : QObject {
        QWidget *row;
        QWidget *label;
        LabelCollapser(QWidget *r, QWidget *l) : QObject(r), row(r), label(l) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Resize)
                label->setVisible(row->width() >= 660);
            return false;
        }
    };
    row->installEventFilter(new LabelCollapser(row, add->findChild<QLabel *>(QStringLiteral("ButtonLabel"))));
    add->setToolTip(tr("Добавить фото с компьютера (или перетащите файлы в это окно)"));
    add->onClick = [this]() { importPhotos(); };
    layout->addWidget(add);

    // design.pen "View Toggle": two 46×46 segments, the active one tinted.
    auto *toggle = new QFrame;
    toggle->setFixedHeight(46);
    styleFrame(toggle, QStringLiteral("ViewToggle"),
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

void PhotosPanel::setListView(bool list)
{
    m_listView = list;
    QSettings().setValue(QStringLiteral("photos/listView"), list);
    const auto paint = [](ClickFrame *button, bool on, const QString &iconName, bool leftEdge) {
        button->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; border-top-%2-radius: 8px; border-bottom-%2-radius: 8px; }")
                                  .arg(on ? Theme::AccentBlueBg : QStringLiteral("#ffffff"),
                                       leftEdge ? QStringLiteral("left") : QStringLiteral("right")));
        button->findChild<QLabel *>(QStringLiteral("ToggleIcon"))
            ->setPixmap(IconProvider::pixmap(iconName, QColor(on ? Theme::AccentBlue : Theme::TextDarkSecondary), 18));
    };
    paint(m_gridButton, !list, QStringLiteral("layout-grid"), true);
    paint(m_listButton, list, QStringLiteral("list"), false);

    // Clear the old layout's items before replacing it.
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
        auto *flow = new FlowLayout(FlowLayout::Mode::EqualColumns, 12, 160, m_items);
        flow->setContentsMargins(0, 0, 0, 16);
        flow->setColumnsFromWidthOnly(true);
    }
    rebuildItems();
}

// ---- Data ------------------------------------------------------------------

void PhotosPanel::reload()
{
    m_photos = m_repository->fetch(QString(), ContentType::Photo, SortOrder::DateAddedDesc);
    QSet<int> existing;
    for (const ContentItem &item : std::as_const(m_photos))
        existing.insert(item.id);
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [&](int id) { return !existing.contains(id); }),
                      m_selection.end());
    rebuildItems();
}

const ContentItem *PhotosPanel::photo(int id) const
{
    for (const ContentItem &item : m_photos) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

QList<ContentItem> PhotosPanel::visiblePhotos() const
{
    QList<ContentItem> result;
    for (const ContentItem &item : m_photos) {
        if (m_search.isEmpty() || item.title.contains(m_search, Qt::CaseInsensitive)
            || item.caption.contains(m_search, Qt::CaseInsensitive))
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

QList<ContentItem> PhotosPanel::selectedItems() const
{
    QList<ContentItem> result;
    for (int id : m_shownIds) {
        if (m_selection.contains(id)) {
            if (const ContentItem *item = photo(id))
                result << *item;
        }
    }
    return result;
}

void PhotosPanel::rebuildItems()
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

    const QList<ContentItem> photos = visiblePhotos();
    for (const ContentItem &item : photos) {
        PhotoItemWidget *widget = m_listView ? static_cast<PhotoItemWidget *>(new PhotoRow(item))
                                             : static_cast<PhotoItemWidget *>(new PhotoCard(item));
        if (!m_listView) {
            QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            widget->setSizePolicy(policy);
        }
        const int id = item.id;
        widget->onClick = [this, id](Qt::KeyboardModifiers modifiers) { clickItem(id, modifiers); };
        widget->onDoubleClick = [this, id]() { startShow(id); };
        widget->onMenu = [this, id](const QPoint &pos) { showItemMenu(id, pos); };
        layout->addWidget(widget);
        m_itemWidgets.insert(id, widget);
        m_shownIds << id;
    }

    if (photos.isEmpty()) {
        m_emptyTitle->setText(m_photos.isEmpty() ? tr("Пока нет фото") : tr("Ничего не найдено"));
        m_emptyHint->setText(m_photos.isEmpty() ? tr("Нажмите «Добавить фото» или перетащите изображения в это окно.")
                                                : tr("Попробуйте другой поисковый запрос."));
        m_stack->setCurrentIndex(1);
    } else {
        m_stack->setCurrentIndex(0);
    }
    refreshSelection();
    m_items->updateGeometry();
}

void PhotosPanel::refreshSelection()
{
    for (auto it = m_itemWidgets.constBegin(); it != m_itemWidgets.constEnd(); ++it)
        static_cast<PhotoItemWidget *>(it.value())->setSelected(m_selection.contains(it.key()));
}

// ---- Interaction -----------------------------------------------------------

void PhotosPanel::clickItem(int id, Qt::KeyboardModifiers modifiers)
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

void PhotosPanel::startShow(int fromId)
{
    // The whole current view as a slideshow, starting at `fromId`, so
    // Назад/Вперёд walk through the gallery in the order shown.
    QList<ContentItem> photos;
    int startIndex = 0;
    for (int id : std::as_const(m_shownIds)) {
        if (const ContentItem *item = photo(id)) {
            if (id == fromId)
                startIndex = photos.size();
            photos << *item;
        }
    }
    if (!photos.isEmpty())
        emit goLiveRequested(photos, startIndex);
}

void PhotosPanel::showSelected()
{
    const QList<ContentItem> selected = selectedItems();
    if (selected.size() == 1)
        startShow(selected.first().id);
    else if (!selected.isEmpty())
        emit goLiveRequested(selected, 0);
}

void PhotosPanel::showItemMenu(int id, const QPoint &globalPos)
{
    const int count = m_selection.size();
    QMenu menu(this);
    if (count > 1) {
        menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16),
                       tr("Показать выбранные (%1)").arg(count), this, &PhotosPanel::showSelected);
    } else {
        menu.addAction(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkPrimary), 16),
                       tr("Показать на экране"), this, [this, id]() { startShow(id); });
    }
    menu.addAction(IconProvider::icon(QStringLiteral("eye"), QColor(Theme::TextDarkPrimary), 16), tr("Предпросмотр"), this,
                   [this, id]() { showPreview(id); });
    if (count <= 1)
        menu.addAction(IconProvider::icon(QStringLiteral("pencil"), QColor(Theme::TextDarkPrimary), 16), tr("Переименовать…"),
                       this, [this, id]() { renamePhoto(id); });
    menu.addSeparator();
    menu.addAction(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 16),
                   count > 1 ? tr("Удалить выбранные (%1)").arg(count) : tr("Удалить"), this, &PhotosPanel::deleteSelection);
    menu.exec(globalPos);
}

void PhotosPanel::showPreview(int id)
{
    QList<ContentItem> photos;
    for (int shownId : std::as_const(m_shownIds)) {
        if (const ContentItem *item = photo(shownId))
            photos << *item;
    }
    if (photos.isEmpty())
        return;
    auto current = std::make_shared<int>(0);
    for (int i = 0; i < photos.size(); ++i) {
        if (photos.at(i).id == id)
            *current = i;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(960, 540);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *render = new SlideRenderWidget(dialog);
    layout->addWidget(render);
    const auto showAt = [dialog, render, photos, current](int delta) {
        *current = (*current + delta + int(photos.size())) % int(photos.size());
        const ContentItem &item = photos.at(*current);
        render->setContent(buildSlideContent(item, QStringList{QString()}, 0, false));
        dialog->setWindowTitle(QObject::tr("Предпросмотр — %1 (%2 / %3) · ← → листать")
                                   .arg(displayName(item)).arg(*current + 1).arg(photos.size()));
    };
    auto *next = new QShortcut(QKeySequence(Qt::Key_Right), dialog);
    connect(next, &QShortcut::activated, dialog, [showAt]() { showAt(1); });
    auto *prev = new QShortcut(QKeySequence(Qt::Key_Left), dialog);
    connect(prev, &QShortcut::activated, dialog, [showAt]() { showAt(-1); });
    showAt(0);
    dialog->show();
}

void PhotosPanel::renamePhoto(int id)
{
    const ContentItem *item = photo(id);
    if (!item)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Фото"), tr("Название:"), QLineEdit::Normal, item->title, &ok).trimmed();
    if (!ok || name.isEmpty() || name == item->title)
        return;
    ContentItem updated = *item;
    updated.title = name;
    m_repository->update(updated);
    reload();
}

void PhotosPanel::deleteSelection()
{
    const QList<ContentItem> selected = selectedItems();
    if (selected.isEmpty())
        return;
    const QString question = selected.size() == 1
        ? tr("Удалить фото «%1» из библиотеки?").arg(displayName(selected.first()))
        : tr("Удалить %1 фото из библиотеки?").arg(selected.size());
    if (QMessageBox::question(this, tr("Удалить"), question) != QMessageBox::Yes)
        return;
    const QString ownDir = QFileInfo(Database::photosDir()).absoluteFilePath();
    for (const ContentItem &item : selected) {
        m_repository->remove(item.id);
        // Only files Sermon copied into its own library folder.
        if (QFileInfo(item.imagePath).absolutePath() == ownDir)
            QFile::remove(item.imagePath);
    }
    m_selection.clear();
    reload();
    emit libraryChanged();
}

// ---- Import ----------------------------------------------------------------

void PhotosPanel::importPhotos()
{
    QStringList patterns;
    for (const QString &ext : imageExtensions())
        patterns << QStringLiteral("*.") + ext;
    importFiles(QFileDialog::getOpenFileNames(this, tr("Добавить фото"), QString(),
                                              tr("Изображения (%1)").arg(patterns.join(QLatin1Char(' ')))));
}

void PhotosPanel::importFiles(const QStringList &paths)
{
    QList<int> added;
    QStringList failed;
    for (const QString &source : paths) {
        const QFileInfo info(source);
        if (!imageExtensions().contains(info.suffix().toLower())) {
            failed << info.fileName();
            continue;
        }
        // Same as ItemEditDialog: copy into Sermon's own folder.
        const QString dest = Database::photosDir() + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QLatin1Char('.') + info.suffix();
        if (!QFile::copy(source, dest)) {
            failed << info.fileName();
            continue;
        }
        ContentItem item;
        item.type = ContentType::Photo;
        item.title = info.completeBaseName();
        item.imagePath = dest;
        if (!m_repository->add(item)) {
            QFile::remove(dest);
            failed << info.fileName();
            continue;
        }
        added << item.id;
    }
    m_selection = added;
    reload();
    if (!added.isEmpty())
        emit libraryChanged();
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Добавить фото"), tr("Не удалось добавить:\n%1").arg(failed.join(QLatin1Char('\n'))));
}

void PhotosPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void PhotosPanel::dropEvent(QDropEvent *event)
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
