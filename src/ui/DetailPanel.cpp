#include "DetailPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int TabContent = 0;
constexpr int TabInfo = 1;
constexpr int TabNotes = 2;

QPushButton *makeOutlineIconButton(const QString &iconName, const QString &tooltip,
                                    const QColor &iconColor = QColor(Theme::TextDarkPrimary), int iconSize = 15)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineIconButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(38, 38);
    button->setIcon(IconProvider::icon(iconName, iconColor, iconSize));
    button->setIconSize(QSize(iconSize, iconSize));
    button->setToolTip(tooltip);
    return button;
}
} // namespace

DetailPanel::DetailPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DetailPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // Uncapped, this panel's stretch=1 in libraryTopRow made it (and by
    // extension m_contentStack) claim far more width than its own content
    // ever needs, starving DisplayControlPanel next to it — see
    // MainWindow.cpp's categoryLayout comment. 700 comfortably covers its
    // content (tabs, lyrics/notes text) at the design's own proportions.
    setMaximumWidth(700);
    buildUi();
    showItem(std::nullopt);
}

void DetailPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(18);

    m_emptyState = new QLabel(tr("Выберите запись слева, чтобы увидеть детали"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(Theme::TextDarkSecondary));
    root->addWidget(m_emptyState);

    m_content = new QWidget;
    root->addWidget(m_content);

    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    // ---- Header ----
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
        if (m_item)
            emit favoriteToggleRequested(m_item->id);
    });
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerRow->addWidget(m_titleLabel, 1, Qt::AlignVCenter);
    headerRow->addWidget(m_starButton, 0, Qt::AlignVCenter);
    headerRow->addStretch();

    // "Редактировать" lives in the "Ещё" menu rather than its own button —
    // a standalone button plus this one left no room for the title on
    // longer song names, clipping it.
    auto *moreButton = makeOutlineIconButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"),
                                              QColor(Theme::TextDarkSecondary), 16);
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton]() {
        if (!m_item)
            return;
        QMenu menu(this);
        QAction *editAction = menu.addAction(tr("Редактировать"));
        connect(editAction, &QAction::triggered, this, [this]() { emit editRequested(m_item->id); });
        QAction *deleteAction = menu.addAction(tr("Удалить"));
        connect(deleteAction, &QAction::triggered, this, [this]() { emit deleteRequested(m_item->id); });
        menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
    });
    headerRow->addWidget(moreButton, 0, Qt::AlignVCenter);
    layout->addLayout(headerRow);

    m_metaLabel = new QLabel;
    m_metaLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkSecondary));
    layout->addWidget(m_metaLabel);

    // ---- Tabs ----
    auto *tabsRow = new QHBoxLayout;
    tabsRow->setSpacing(28);
    m_tabContent = new QPushButton(tr("Текст и слайды"));
    m_tabInfo = new QPushButton(tr("Информация"));
    m_tabNotes = new QPushButton(tr("Заметки"));
    for (QPushButton *tab : {m_tabContent, m_tabInfo, m_tabNotes}) {
        tab->setCheckable(true);
        tab->setObjectName(QStringLiteral("TabButton"));
        tab->setCursor(Qt::PointingHandCursor);
        tabsRow->addWidget(tab);
    }
    tabsRow->addStretch();
    layout->addLayout(tabsRow);

    auto *tabsDivider = new QWidget;
    tabsDivider->setFixedHeight(1);
    tabsDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    layout->addWidget(tabsDivider);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->addButton(m_tabContent, TabContent);
    tabGroup->addButton(m_tabInfo, TabInfo);
    tabGroup->addButton(m_tabNotes, TabNotes);
    m_tabContent->setChecked(true);

    m_tabStack = new QStackedWidget;
    layout->addWidget(m_tabStack, 1);
    connect(tabGroup, &QButtonGroup::idClicked, m_tabStack, &QStackedWidget::setCurrentIndex);

    // ---- Tab: content ----
    auto *page0 = new QWidget;
    auto *page0Layout = new QVBoxLayout(page0);
    page0Layout->setContentsMargins(0, 0, 0, 0);
    page0Layout->setSpacing(14);

    m_lyricsBox = new QTextEdit;
    m_lyricsBox->setReadOnly(true);
    m_lyricsBox->setObjectName(QStringLiteral("LyricsBox"));
    page0Layout->addWidget(m_lyricsBox, 1);

    m_photoBox = new QLabel;
    m_photoBox->setAlignment(Qt::AlignCenter);
    m_photoBox->setMinimumHeight(260);
    m_photoBox->setMaximumHeight(420);
    m_photoBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_photoBox->setObjectName(QStringLiteral("LyricsBox"));
    page0Layout->addWidget(m_photoBox);

    m_tabStack->insertWidget(TabContent, page0);

    // ---- Tab: info ----
    auto *page1 = new QWidget;
    auto *page1Layout = new QVBoxLayout(page1);
    m_infoText = new QLabel;
    m_infoText->setWordWrap(true);
    m_infoText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_infoText->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkPrimary));
    page1Layout->addWidget(m_infoText);
    page1Layout->addStretch();
    m_tabStack->insertWidget(TabInfo, page1);

    // ---- Tab: notes ----
    auto *page2 = new QWidget;
    auto *page2Layout = new QVBoxLayout(page2);
    m_notesEdit = new QTextEdit;
    m_notesEdit->setPlaceholderText(tr("Заметки для оператора (не выводятся на экран)..."));
    page2Layout->addWidget(m_notesEdit);
    m_tabStack->insertWidget(TabNotes, page2);

    m_notesSaveTimer = new QTimer(this);
    m_notesSaveTimer->setSingleShot(true);
    m_notesSaveTimer->setInterval(600);
    connect(m_notesEdit, &QTextEdit::textChanged, this, [this]() { m_notesSaveTimer->start(); });
    connect(m_notesSaveTimer, &QTimer::timeout, this, [this]() {
        if (m_item)
            emit notesChanged(m_item->id, m_notesEdit->toPlainText());
    });

    setStyleSheet(QStringLiteral(R"(
        QWidget#DetailPanel { background: #ffffff; border: 1px solid %2; border-radius: 14px; }
        QPushButton#OutlineButton, QPushButton#OutlineIconButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 16px; font-weight: 600; font-size: 13.5px; color: %1;
        }
        QPushButton#OutlineIconButton { padding: 9px; }
        QPushButton#OutlineButton:hover, QPushButton#OutlineIconButton:hover { background: #f3f4f6; }
        QPushButton#TabButton {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 0 0 10px 0; font-size: 14px; font-weight: 500; color: %4;
        }
        QPushButton#TabButton:checked { color: %3; font-weight: 600; border-bottom: 2px solid %3; }
        QTextEdit#LyricsBox, QLabel#LyricsBox {
            border: 1px solid %2; border-radius: 12px; padding: 20px;
            font-size: 14.5px; color: %1;
        }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary));
}

void DetailPanel::showItem(const std::optional<ContentItem> &item)
{
    if (m_notesSaveTimer->isActive()) {
        m_notesSaveTimer->stop();
        if (m_item)
            emit notesChanged(m_item->id, m_notesEdit->toPlainText());
    }

    m_item = item;

    if (!item) {
        m_emptyState->show();
        m_content->hide();
        return;
    }

    m_emptyState->hide();
    m_content->show();

    m_titleLabel->setText(item->displayTitle());
    m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"),
                                              QColor(item->favorite ? QStringLiteral("#f5a623") : Theme::TextDarkSecondary),
                                              20, item->favorite));

    const QString category = contentTypeDisplayName(item->type);
    m_metaLabel->setText(tr("%1 · добавлено %2").arg(category, item->createdAt.toString(QStringLiteral("dd.MM.yyyy"))));

    const bool isPhoto = item->type == ContentType::Photo;
    m_tabContent->setText(isPhoto ? tr("Просмотр") : tr("Текст и слайды"));

    QStringList infoLines;
    infoLines << tr("Категория: %1").arg(category);
    infoLines << tr("Добавлено: %1").arg(item->createdAt.toString(QStringLiteral("dd.MM.yyyy HH:mm")));

    switch (item->type) {
    case ContentType::Song: {
        QStringList slides = item->slides();
        if (slides.isEmpty())
            slides << QString();
        m_lyricsBox->setPlainText(slides.join(QStringLiteral("\n\n")));
        m_lyricsBox->show();
        m_photoBox->hide();
        if (!item->refLocation.isEmpty())
            infoLines << tr("Номер в сборнике: %1").arg(item->refLocation);
        break;
    }
    case ContentType::BibleVerse: {
        QStringList slides = item->slides();
        if (slides.isEmpty())
            slides << QString();
        m_lyricsBox->setPlainText(slides.join(QStringLiteral("\n\n")));
        m_lyricsBox->show();
        m_photoBox->hide();
        infoLines << tr("Ссылка: %1 %2").arg(item->refBook, item->refLocation);
        break;
    }

    case ContentType::Announcement:
        m_lyricsBox->setPlainText(item->text);
        m_lyricsBox->show();
        m_photoBox->hide();
        infoLines << (item->expiryDate.isValid()
                          ? tr("Актуально до: %1").arg(item->expiryDate.toString(QStringLiteral("dd.MM.yyyy")))
                          : tr("Без срока действия"));
        break;

    case ContentType::Photo:
        m_lyricsBox->hide();
        m_photoBox->show();
        if (!item->imagePath.isEmpty() && QFileInfo::exists(item->imagePath)) {
            QPixmap pix(item->imagePath);
            m_photoBox->setPixmap(pix.scaled(m_photoBox->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const QImageReader reader(item->imagePath);
            const QSize dims = reader.size();
            const qint64 bytes = QFileInfo(item->imagePath).size();
            m_metaLabel->setText(tr("%1 · %2×%3 · %4 МБ")
                .arg(category)
                .arg(dims.width())
                .arg(dims.height())
                .arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1)));
        } else {
            m_photoBox->setText(tr("Изображение не найдено"));
        }
        if (!item->caption.isEmpty())
            infoLines << tr("Подпись: %1").arg(item->caption);
        break;

    case ContentType::Video:
        m_lyricsBox->hide();
        m_photoBox->hide();
        break;
    }

    m_infoText->setText(infoLines.join(QStringLiteral("\n")));

    m_notesEdit->blockSignals(true);
    m_notesEdit->setPlainText(item->notes);
    m_notesEdit->blockSignals(false);
}
