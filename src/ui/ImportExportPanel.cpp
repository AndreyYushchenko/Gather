#include "ImportExportPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    return label;
}

QFrame *makeCard()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("IECard"));
    return card;
}

} // namespace

// A clickable "drop zone" placeholder — picking a backup folder is the only
// real import path today, so this opens the same flow as the Export button.
class Dropzone : public QFrame {
    Q_OBJECT
public:
    explicit Dropzone(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("Dropzone"));
        setCursor(Qt::PointingHandCursor);
        auto *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(10);

        auto *iconCircle = new QLabel;
        iconCircle->setFixedSize(56, 56);
        iconCircle->setAlignment(Qt::AlignCenter);
        iconCircle->setStyleSheet(QStringLiteral("background: %1; border-radius: 28px;").arg(Theme::AccentBlueBg));
        iconCircle->setPixmap(IconProvider::pixmap(QStringLiteral("cloud-upload"), QColor(Theme::AccentBlue), 24));
        layout->addWidget(iconCircle, 0, Qt::AlignCenter);

        auto *title = new QLabel(tr("Нажмите, чтобы выбрать папку с резервной копией"));
        title->setAlignment(Qt::AlignCenter);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size: 13.5px; font-weight: 600; color: %1;").arg(Theme::TextDarkPrimary));
        layout->addWidget(title);

        auto *sub = new QLabel(tr("Восстанавливает базу данных и фотографии из папки резервной копии Sermon"));
        sub->setAlignment(Qt::AlignCenter);
        sub->setWordWrap(true);
        sub->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(Theme::TextDarkSecondary));
        layout->addWidget(sub);
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }
};

ImportExportPanel::ImportExportPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ImportExportPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void ImportExportPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(4);

    auto *title = new QLabel(tr("Импорт / Экспорт"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);
    auto *subtitle = new QLabel(tr("Перенос данных между устройствами и форматами"));
    subtitle->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkSecondary));
    root->addWidget(subtitle);
    root->addSpacing(20);

    auto *bodyRow = new QHBoxLayout;
    bodyRow->setSpacing(28);
    root->addLayout(bodyRow, 1);

    // ---- Import column ----
    auto *importCol = new QVBoxLayout;
    importCol->setSpacing(14);
    importCol->addWidget(heading(tr("Импорт")));

    auto *dropzone = new Dropzone;
    dropzone->setMinimumHeight(180);
    connect(dropzone, &Dropzone::clicked, this, &ImportExportPanel::actionRequested);
    importCol->addWidget(dropzone);

    importCol->addWidget(heading(tr("Недавние операции")));
    auto *historyCard = makeCard();
    auto *historyLayout = new QVBoxLayout(historyCard);
    auto *historyEmpty = new QLabel(tr("Здесь появится история экспортов и импортов за эту сессию."));
    historyEmpty->setWordWrap(true);
    historyEmpty->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1;").arg(Theme::TextDarkSecondary));
    historyLayout->addWidget(historyEmpty);
    importCol->addWidget(historyCard);
    importCol->addStretch();
    bodyRow->addLayout(importCol, 1);

    // ---- Export column ----
    auto *exportCol = new QVBoxLayout;
    exportCol->setSpacing(14);
    exportCol->addWidget(heading(tr("Экспорт")));

    auto *whatLabel = new QLabel(tr("Что экспортировать"));
    whatLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: %1;").arg(Theme::TextDarkSecondary));
    exportCol->addWidget(whatLabel);

    auto *checklistCard = makeCard();
    auto *checklistLayout = new QVBoxLayout(checklistCard);
    checklistLayout->setSpacing(10);

    auto makeItemRow = [&](const QString &iconName, const QString &label, QLabel *&countOut) {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        auto *check = new QCheckBox;
        check->setChecked(true);
        row->addWidget(check);
        auto *icon = new QLabel;
        icon->setPixmap(IconProvider::pixmap(iconName, QColor(Theme::TextDarkSecondary), 16));
        row->addWidget(icon);
        auto *text = new QLabel(label);
        text->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkPrimary));
        row->addWidget(text, 1);
        countOut = new QLabel(QStringLiteral("0"));
        countOut->setStyleSheet(QStringLiteral("font-size: 13px; color: %1; font-weight: 600;").arg(Theme::TextDarkSecondary));
        row->addWidget(countOut);
        checklistLayout->addLayout(row);
    };
    makeItemRow(QStringLiteral("music"), tr("Песни"), m_songsCount);
    makeItemRow(QStringLiteral("book-open"), tr("Стихи из Библии (избранное)"), m_bibleCount);
    makeItemRow(QStringLiteral("megaphone"), tr("Объявления"), m_announcementsCount);
    makeItemRow(QStringLiteral("image"), tr("Фото"), m_photosCount);
    makeItemRow(QStringLiteral("list-music"), tr("Плейлисты"), m_playlistsCount);

    auto *settingsRow = new QHBoxLayout;
    settingsRow->setSpacing(10);
    auto *settingsCheck = new QCheckBox;
    settingsCheck->setChecked(true);
    settingsRow->addWidget(settingsCheck);
    auto *settingsIcon = new QLabel;
    settingsIcon->setPixmap(IconProvider::pixmap(QStringLiteral("settings"), QColor(Theme::TextDarkSecondary), 16));
    settingsRow->addWidget(settingsIcon);
    auto *settingsText = new QLabel(tr("Настройки"));
    settingsText->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkPrimary));
    settingsRow->addWidget(settingsText, 1);
    checklistLayout->addLayout(settingsRow);

    exportCol->addWidget(checklistCard);

    auto *formatLabel = new QLabel(tr("Формат экспорта"));
    formatLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkSecondary));
    exportCol->addWidget(formatLabel);
    auto *formatBox = new QLabel(tr("Папка с резервной копией (.db + фото)"));
    formatBox->setStyleSheet(QStringLiteral(
        "border: 1px solid %1; border-radius: 9px; padding: 9px 12px; font-size: 13px; color: %2; background: #ffffff;")
        .arg(Theme::BorderLight, Theme::TextDarkPrimary));
    exportCol->addWidget(formatBox);

    auto *exportButton = new QPushButton(tr("  Экспортировать"));
    exportButton->setObjectName(QStringLiteral("PrimaryButton"));
    exportButton->setCursor(Qt::PointingHandCursor);
    exportButton->setIcon(IconProvider::icon(QStringLiteral("download"), QColor(Theme::TextLightPrimary), 15));
    connect(exportButton, &QPushButton::clicked, this, &ImportExportPanel::actionRequested);
    exportCol->addWidget(exportButton);
    exportCol->addStretch();

    bodyRow->addLayout(exportCol, 1);

    setStyleSheet(QStringLiteral(R"(
        QWidget#ImportExportPanel { background: #ffffff; }
        QFrame#IECard { background: %1; border: 1px solid %2; border-radius: 12px; }
        QFrame#Dropzone { background: %1; border: 1.5px dashed %2; border-radius: 12px; }
        QFrame#Dropzone:hover { border-color: %3; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 10px 16px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
    )").arg(Theme::BgPanel, Theme::BorderLight, Theme::AccentBlue));
}

void ImportExportPanel::setCounts(const QMap<ContentType, int> &counts, int playlistCount)
{
    if (m_songsCount)
        m_songsCount->setText(QString::number(counts.value(ContentType::Song, 0)));
    if (m_bibleCount)
        m_bibleCount->setText(QString::number(counts.value(ContentType::BibleVerse, 0)));
    if (m_announcementsCount)
        m_announcementsCount->setText(QString::number(counts.value(ContentType::Announcement, 0)));
    if (m_photosCount)
        m_photosCount->setText(QString::number(counts.value(ContentType::Photo, 0)));
    if (m_playlistsCount)
        m_playlistsCount->setText(QString::number(playlistCount));
}

#include "ImportExportPanel.moc"
