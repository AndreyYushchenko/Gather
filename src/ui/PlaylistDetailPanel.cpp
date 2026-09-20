#include "PlaylistDetailPanel.h"
#include "IconProvider.h"
#include "Theme.h"
#include "display/SlideContentBuilder.h"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPushButton *makeOutlineButton(const QString &iconName, const QString &text)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineButton"));
    button->setCursor(Qt::PointingHandCursor);
    if (!iconName.isEmpty()) {
        button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextDarkPrimary), 15));
        button->setIconSize(QSize(15, 15));
    }
    button->setText(text);
    return button;
}

QPushButton *makeOutlineIconButton(const QString &iconName, const QString &tooltip)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineIconButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(38, 38);
    button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextDarkPrimary), 15));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(tooltip);
    return button;
}

QString typeIconFor(ContentType type)
{
    switch (type) {
    case ContentType::Song: return QStringLiteral("music");
    case ContentType::BibleVerse: return QStringLiteral("book-open");
    case ContentType::Announcement: return QStringLiteral("megaphone");
    case ContentType::Photo: return QStringLiteral("image");
    case ContentType::Video: return QStringLiteral("video");
    }
    return QStringLiteral("music");
}

} // namespace

// Not in an anonymous namespace: moc cannot generate metaobject code for
// Q_OBJECT classes declared inside one.
class PlaylistEntryRow : public QFrame {
    Q_OBJECT
public:
    explicit PlaylistEntryRow(const PlaylistEntry &entry, int order, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_rowId(entry.rowId)
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(12);

        auto *handle = new QLabel(QStringLiteral("⋮⋮"));
        handle->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Theme::TextDarkSecondary));
        handle->setFixedWidth(14);
        layout->addWidget(handle);

        auto *orderLabel = new QLabel(QString::number(order));
        orderLabel->setFixedWidth(18);
        orderLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 600;").arg(Theme::TextDarkSecondary));
        layout->addWidget(orderLabel);

        auto *typeIcon = new QLabel;
        typeIcon->setPixmap(IconProvider::pixmap(typeIconFor(entry.item.type), QColor(Theme::TextDarkSecondary), 16));
        layout->addWidget(typeIcon);

        auto *textCol = new QVBoxLayout;
        textCol->setSpacing(1);
        m_titleLabel = new QLabel(entry.item.displayTitle());
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13.5px;"));
        auto *typeLabel = new QLabel(contentTypeDisplayName(entry.item.type));
        typeLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11.5px;").arg(Theme::TextDarkSecondary));
        textCol->addWidget(m_titleLabel);
        textCol->addWidget(typeLabel);
        layout->addLayout(textCol, 1);

        auto *removeButton = new QPushButton;
        removeButton->setFlat(true);
        removeButton->setCursor(Qt::PointingHandCursor);
        removeButton->setFixedSize(24, 24);
        removeButton->setIcon(IconProvider::icon(QStringLiteral("trash-2"), QColor(Theme::TextDarkSecondary), 15));
        removeButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
        connect(removeButton, &QPushButton::clicked, this, [this]() { emit removeClicked(m_rowId); });
        layout->addWidget(removeButton);

        setSelected(false);
    }

    void setSelected(bool selected)
    {
        if (m_selectionApplied && m_selected == selected)
            return;
        m_selected = selected;
        m_selectionApplied = true;
        setStyleSheet(selected
            ? QStringLiteral("PlaylistEntryRow { background: %1; border-radius: 9px; }").arg(Theme::AccentBlueBg)
            : QStringLiteral("PlaylistEntryRow { background: transparent; border-radius: 9px; }"));
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13.5px; color: %1;")
                                         .arg(selected ? Theme::AccentBlue : Theme::TextDarkPrimary));
    }

    int rowId() const { return m_rowId; }

signals:
    void clicked(int rowId);
    void removeClicked(int rowId);

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(m_rowId); }

private:
    int m_rowId;
    bool m_selected = false;
    bool m_selectionApplied = false;
    QLabel *m_titleLabel = nullptr;
};

PlaylistDetailPanel::PlaylistDetailPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PlaylistDetailPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void PlaylistDetailPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(16);

    m_emptyState = new QLabel(tr("Выберите плейлист слева, чтобы увидеть детали"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(Theme::TextDarkSecondary));
    root->addWidget(m_emptyState);

    m_content = new QWidget;
    root->addWidget(m_content);
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    m_starButton = new QPushButton;
    m_starButton->setFlat(true);
    m_starButton->setCursor(Qt::PointingHandCursor);
    m_starButton->setFixedSize(24, 24);
    m_starButton->setIconSize(QSize(20, 20));
    m_starButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_starButton, &QPushButton::clicked, this, [this]() {
        if (m_playlist)
            emit favoriteToggleRequested(m_playlist->id);
    });
    headerRow->addWidget(m_titleLabel);
    headerRow->addWidget(m_starButton);
    headerRow->addStretch();

    auto *editButton = makeOutlineButton(QStringLiteral("pencil"), tr("Переименовать"));
    connect(editButton, &QPushButton::clicked, this, [this]() {
        if (!m_playlist)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Переименовать плейлист"), tr("Название:"),
                                                     QLineEdit::Normal, m_playlist->name, &ok);
        if (ok && !name.trimmed().isEmpty())
            emit renameRequested(m_playlist->id, name.trimmed());
    });
    headerRow->addWidget(editButton);

    auto *moreButton = makeOutlineIconButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"));
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton]() {
        if (!m_playlist)
            return;
        QMenu menu(this);
        QAction *dup = menu.addAction(tr("Дублировать плейлист"));
        connect(dup, &QAction::triggered, this, [this]() { emit duplicateRequested(m_playlist->id); });
        QAction *del = menu.addAction(tr("Удалить плейлист"));
        connect(del, &QAction::triggered, this, [this]() {
            const auto reply = QMessageBox::question(this, tr("Удаление"), tr("Удалить плейлист «%1»?").arg(m_playlist->name));
            if (reply == QMessageBox::Yes)
                emit deleteRequested(m_playlist->id);
        });
        menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
    });
    headerRow->addWidget(moreButton);
    layout->addLayout(headerRow);

    m_metaLabel = new QLabel;
    m_metaLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkSecondary));
    layout->addWidget(m_metaLabel);

    auto *heading = new QLabel(tr("Порядок показа"));
    heading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    layout->addWidget(heading);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("PlaylistOrderList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(2);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(m_list, 1);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        if (!m_playlist)
            return;
        QList<int> order;
        for (int i = 0; i < m_list->count(); ++i) {
            if (auto *row = qobject_cast<PlaylistEntryRow *>(m_list->itemWidget(m_list->item(i))))
                order << row->rowId();
        }
        emit reorderRequested(m_playlist->id, order);
    });

    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(10);
    auto *addButton = makeOutlineButton(QStringLiteral("plus"), tr("Добавить"));
    connect(addButton, &QPushButton::clicked, this, [this]() {
        if (!m_playlist)
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Добавить в плейлист"));
        dialog.resize(420, 480);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *pickList = new QListWidget(&dialog);
        for (const ContentItem &item : std::as_const(m_libraryItems)) {
            auto *listItem = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(item.displayTitle(), contentTypeDisplayName(item.type)));
            listItem->setData(Qt::UserRole, item.id);
            pickList->addItem(listItem);
        }
        dialogLayout->addWidget(pickList);
        connect(pickList, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
        auto *buttons = new QHBoxLayout;
        auto *cancelButton = new QPushButton(tr("Отмена"));
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
        auto *okButton = new QPushButton(tr("Добавить"));
        okButton->setDefault(true);
        connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(okButton);
        dialogLayout->addLayout(buttons);

        if (dialog.exec() == QDialog::Accepted && pickList->currentItem())
            emit addItemRequested(m_playlist->id, pickList->currentItem()->data(Qt::UserRole).toInt());
    });
    actionsRow->addWidget(addButton);

    auto *copyButton = makeOutlineIconButton(QStringLiteral("copy"), tr("Повторить выбранный элемент"));
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (!m_playlist || m_selectedRowId < 0)
            return;
        for (const PlaylistEntry &entry : std::as_const(m_entries))
            if (entry.rowId == m_selectedRowId)
                emit addItemRequested(m_playlist->id, entry.item.id);
    });
    actionsRow->addWidget(copyButton);

    auto *deleteButton = makeOutlineIconButton(QStringLiteral("trash-2"), tr("Удалить выбранный элемент"));
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        if (m_selectedRowId >= 0)
            emit removeEntryRequested(m_selectedRowId);
    });
    actionsRow->addWidget(deleteButton);
    actionsRow->addStretch();

    auto *previewButton = makeOutlineButton(QStringLiteral("eye"), tr("Предпросмотр"));
    connect(previewButton, &QPushButton::clicked, this, [this]() {
        for (const PlaylistEntry &entry : std::as_const(m_entries)) {
            if (entry.rowId == m_selectedRowId) {
                emit previewRequested(entry.item, 0);
                return;
            }
        }
    });
    actionsRow->addWidget(previewButton);

    auto *onScreenButton = new QPushButton(tr("На экран"));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    onScreenButton->setCursor(Qt::PointingHandCursor);
    onScreenButton->setIcon(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextLightPrimary), 15));
    onScreenButton->setIconSize(QSize(15, 15));
    connect(onScreenButton, &QPushButton::clicked, this, &PlaylistDetailPanel::triggerGoLive);
    actionsRow->addWidget(onScreenButton);

    layout->addLayout(actionsRow);

    setStyleSheet(QStringLiteral(R"(
        QWidget#PlaylistDetailPanel { background: #ffffff; }
        QPushButton#OutlineButton, QPushButton#OutlineIconButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 16px; font-weight: 600; font-size: 13.5px; color: %1;
        }
        QPushButton#OutlineIconButton { padding: 9px; }
        QPushButton#OutlineButton:hover, QPushButton#OutlineIconButton:hover { background: #f3f4f6; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 9px 16px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
        QListWidget#PlaylistOrderList { background: transparent; border: none; }
        QListWidget#PlaylistOrderList::item { border: none; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue));
}

void PlaylistDetailPanel::setLibraryItems(const QList<ContentItem> &items)
{
    m_libraryItems = items;
}

void PlaylistDetailPanel::triggerGoLive()
{
    for (const PlaylistEntry &entry : std::as_const(m_entries)) {
        if (entry.rowId == m_selectedRowId) {
            emit goLiveRequested(entry.item, 0);
            return;
        }
    }
}

void PlaylistDetailPanel::showPlaylist(const std::optional<Playlist> &playlist, const QList<PlaylistEntry> &entries)
{
    m_playlist = playlist;
    m_entries = entries;

    if (!playlist) {
        m_emptyState->show();
        m_content->hide();
        return;
    }
    m_emptyState->hide();
    m_content->show();

    m_titleLabel->setText(playlist->name);
    m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"),
        QColor(playlist->favorite ? QStringLiteral("#f5a623") : Theme::TextDarkSecondary), 20, playlist->favorite));
    m_metaLabel->setText(tr("Плейлист · %1 элементов · создан %2")
        .arg(entries.size()).arg(playlist->createdAt.toString(QStringLiteral("dd.MM.yyyy"))));

    if (m_selectedRowId < 0 && !m_entries.isEmpty())
        m_selectedRowId = m_entries.first().rowId;
    rebuildList();
}

void PlaylistDetailPanel::rebuildList()
{
    // See LibraryListPanel::setItems for why: without this the list
    // repaints/relayouts after every single addItem() below.
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    int order = 1;
    for (const PlaylistEntry &entry : std::as_const(m_entries)) {
        auto *row = new PlaylistEntryRow(entry, order++);
        row->setSelected(entry.rowId == m_selectedRowId);
        connect(row, &PlaylistEntryRow::clicked, this, [this](int rowId) { selectRow(rowId); });
        connect(row, &PlaylistEntryRow::removeClicked, this, [this](int rowId) { emit removeEntryRequested(rowId); });

        auto *listItem = new QListWidgetItem;
        listItem->setSizeHint(row->sizeHint());
        listItem->setFlags(listItem->flags() | Qt::ItemIsDragEnabled);
        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, row);
    }
    m_list->setUpdatesEnabled(true);
}

void PlaylistDetailPanel::selectRow(int rowId)
{
    m_selectedRowId = rowId;
    for (int i = 0; i < m_list->count(); ++i) {
        if (auto *row = qobject_cast<PlaylistEntryRow *>(m_list->itemWidget(m_list->item(i))))
            row->setSelected(row->rowId() == rowId);
    }
}

#include "PlaylistDetailPanel.moc"
