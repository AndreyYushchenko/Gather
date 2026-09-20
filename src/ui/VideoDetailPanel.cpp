#include "VideoDetailPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QAudioOutput>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {
constexpr int TabPreview = 0;
constexpr int TabInfo = 1;
constexpr int TabNotes = 2;

bool isRemoteSource(const QString &path)
{
    return path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"));
}

QString formatTime(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60, 2, 10, QLatin1Char('0')).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

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
}

VideoDetailPanel::VideoDetailPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("VideoDetailPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    showItem(std::nullopt);
}

void VideoDetailPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(16);

    m_emptyState = new QLabel(tr("Выберите видео слева, чтобы увидеть детали"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(Theme::TextDarkSecondary));
    root->addWidget(m_emptyState);

    m_content = new QWidget;
    root->addWidget(m_content);
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

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
    headerRow->addWidget(m_titleLabel);
    headerRow->addWidget(m_starButton);
    headerRow->addStretch();

    auto *editButton = makeOutlineButton(QStringLiteral("pencil"), tr("Переименовать"));
    connect(editButton, &QPushButton::clicked, this, [this]() {
        if (!m_item)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Переименовать видео"), tr("Название:"),
                                                     QLineEdit::Normal, m_item->title, &ok);
        if (ok && !name.trimmed().isEmpty())
            emit titleChangeRequested(m_item->id, name.trimmed());
    });
    headerRow->addWidget(editButton);

    auto *moreButton = makeOutlineIconButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"));
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton]() {
        if (!m_item)
            return;
        QMenu menu(this);
        QAction *deleteAction = menu.addAction(tr("Удалить"));
        connect(deleteAction, &QAction::triggered, this, [this]() { emit deleteRequested(m_item->id); });
        menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
    });
    headerRow->addWidget(moreButton);
    layout->addLayout(headerRow);

    m_metaLabel = new QLabel;
    m_metaLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkSecondary));
    layout->addWidget(m_metaLabel);

    // ---- Output mode ----
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(12);
    auto *modeLabel = new QLabel(tr("Режим показа видео:"));
    modeLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 500;").arg(Theme::TextDarkSecondary));
    modeRow->addWidget(modeLabel);

    m_modeBackground = new QPushButton(tr("Как фон (в цикле)"));
    m_modeBackground->setObjectName(QStringLiteral("ModeButton"));
    m_modeBackground->setCheckable(true);
    m_modeBackground->setCursor(Qt::PointingHandCursor);
    m_modeBackground->setIcon(IconProvider::icon(QStringLiteral("repeat"), QColor(Theme::TextDarkSecondary), 14));
    m_modeBackground->setIconSize(QSize(14, 14));

    m_modeFullscreen = new QPushButton(tr("Прямой показ на экран"));
    m_modeFullscreen->setObjectName(QStringLiteral("ModeButton"));
    m_modeFullscreen->setCheckable(true);
    m_modeFullscreen->setChecked(true);
    m_modeFullscreen->setCursor(Qt::PointingHandCursor);
    m_modeFullscreen->setIcon(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextDarkSecondary), 14));
    m_modeFullscreen->setIconSize(QSize(14, 14));

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_modeBackground);
    modeGroup->addButton(m_modeFullscreen);
    connect(m_modeBackground, &QPushButton::toggled, this, [this](bool on) { if (on) m_loopMode = true; });
    connect(m_modeFullscreen, &QPushButton::toggled, this, [this](bool on) { if (on) m_loopMode = false; });

    modeRow->addWidget(m_modeBackground);
    modeRow->addWidget(m_modeFullscreen);
    modeRow->addStretch();
    layout->addLayout(modeRow);

    // ---- Tabs ----
    auto *tabsRow = new QHBoxLayout;
    tabsRow->setSpacing(28);
    m_tabPreview = new QPushButton(tr("Просмотр"));
    m_tabInfo = new QPushButton(tr("Информация"));
    m_tabNotes = new QPushButton(tr("Заметки"));
    for (QPushButton *tab : {m_tabPreview, m_tabInfo, m_tabNotes}) {
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
    tabGroup->addButton(m_tabPreview, TabPreview);
    tabGroup->addButton(m_tabInfo, TabInfo);
    tabGroup->addButton(m_tabNotes, TabNotes);
    m_tabPreview->setChecked(true);

    m_tabStack = new QStackedWidget;
    layout->addWidget(m_tabStack, 1);
    connect(tabGroup, &QButtonGroup::idClicked, m_tabStack, &QStackedWidget::setCurrentIndex);

    // ---- Tab: preview ----
    auto *page0 = new QWidget;
    auto *page0Layout = new QVBoxLayout(page0);
    page0Layout->setContentsMargins(0, 0, 0, 0);
    page0Layout->setSpacing(10);

    m_previewBox = new QWidget;
    m_previewBox->setObjectName(QStringLiteral("PreviewBox"));
    m_previewBox->setFixedHeight(360);
    auto *previewLayout = new QVBoxLayout(m_previewBox);
    previewLayout->setContentsMargins(0, 0, 0, 0);

    m_mediaPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);
    m_videoWidget = new QVideoWidget;
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    previewLayout->addWidget(m_videoWidget);

    m_remoteNotice = new QLabel(tr("Видео по ссылке YouTube воспроизводится через вывод на OBS/браузер.\n"
                                    "Локальный предпросмотр недоступен — используйте «Предпросмотр» на веб-адресе показа."));
    m_remoteNotice->setAlignment(Qt::AlignCenter);
    m_remoteNotice->setWordWrap(true);
    m_remoteNotice->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; padding: 24px;").arg(Theme::TextLightSecondary));
    previewLayout->addWidget(m_remoteNotice);
    page0Layout->addWidget(m_previewBox);

    auto *transportRow = new QHBoxLayout;
    transportRow->setSpacing(10);
    m_playButton = new QPushButton;
    m_playButton->setObjectName(QStringLiteral("OutlineIconButton"));
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(38, 38);
    m_playButton->setIconSize(QSize(15, 15));
    m_playButton->setIcon(IconProvider::icon(QStringLiteral("play"), QColor(Theme::TextDarkPrimary), 15));
    connect(m_playButton, &QPushButton::clicked, this, &VideoDetailPanel::togglePlayback);
    transportRow->addWidget(m_playButton);

    m_timeCurrentLabel = new QLabel(QStringLiteral("00:00"));
    m_timeCurrentLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11.5px; font-weight: 600;").arg(Theme::TextDarkSecondary));
    transportRow->addWidget(m_timeCurrentLabel);

    m_timelineSlider = new QSlider(Qt::Horizontal);
    m_timelineSlider->setRange(0, 0);
    connect(m_timelineSlider, &QSlider::sliderPressed, this, [this]() { m_scrubbing = true; });
    connect(m_timelineSlider, &QSlider::sliderReleased, this, [this]() {
        m_scrubbing = false;
        m_mediaPlayer->setPosition(m_timelineSlider->value());
    });
    transportRow->addWidget(m_timelineSlider, 1);

    m_timeTotalLabel = new QLabel(QStringLiteral("00:00"));
    m_timeTotalLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11.5px; font-weight: 600;").arg(Theme::TextDarkSecondary));
    transportRow->addWidget(m_timeTotalLabel);
    page0Layout->addLayout(transportRow);

    connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_timelineSlider->setRange(0, static_cast<int>(duration));
        m_timeTotalLabel->setText(formatTime(duration));
        updateMetaLabel();
    });
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!m_scrubbing)
            m_timelineSlider->setValue(static_cast<int>(position));
        m_timeCurrentLabel->setText(formatTime(position));
    });
    connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        m_playButton->setIcon(IconProvider::icon(state == QMediaPlayer::PlayingState ? QStringLiteral("pause") : QStringLiteral("play"),
                                                   QColor(Theme::TextDarkPrimary), 15));
    });
    connect(m_mediaPlayer, &QMediaPlayer::metaDataChanged, this, &VideoDetailPanel::updateMetaLabel);

    m_tabStack->insertWidget(TabPreview, page0);

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

    // ---- Actions ----
    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(10);
    auto *replaceButton = makeOutlineButton(QStringLiteral("upload"), tr("Заменить видео"));
    connect(replaceButton, &QPushButton::clicked, this, [this]() {
        if (!m_item)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать видео"), QString(),
                                                            tr("Видео (*.mp4 *.mov *.avi *.mkv *.webm)"));
        if (path.isEmpty())
            return;
        emit replaceRequested(m_item->id, path);
    });
    actionsRow->addWidget(replaceButton);
    actionsRow->addStretch();

    auto *previewButton = makeOutlineButton(QStringLiteral("eye"), tr("Предпросмотр"));
    connect(previewButton, &QPushButton::clicked, this, [this]() {
        if (m_item)
            emit previewRequested(*m_item, m_loopMode);
    });
    actionsRow->addWidget(previewButton);

    auto *onScreenButton = new QPushButton(tr("На экран"));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    onScreenButton->setCursor(Qt::PointingHandCursor);
    onScreenButton->setIcon(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextLightPrimary), 15));
    onScreenButton->setIconSize(QSize(15, 15));
    connect(onScreenButton, &QPushButton::clicked, this, &VideoDetailPanel::triggerGoLive);
    actionsRow->addWidget(onScreenButton);

    layout->addLayout(actionsRow);

    setStyleSheet(QStringLiteral(R"(
        QWidget#VideoDetailPanel { background: #ffffff; }
        QWidget#PreviewBox { background: #0b0e14; border-radius: 12px; }
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
        QPushButton#ModeButton {
            background: %5; border: 1px solid %2; border-radius: 7px; padding: 7px 13px;
            font-weight: 600; font-size: 12.5px; color: %4;
        }
        QPushButton#ModeButton:checked { background: %3; border-color: %3; color: #ffffff; }
        QPushButton#TabButton {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 0 0 10px 0; font-size: 14px; font-weight: 500; color: %4;
        }
        QPushButton#TabButton:checked { color: %3; font-weight: 600; border-bottom: 2px solid %3; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary, Theme::BgPanel));

    updateMetaLabel();
}

void VideoDetailPanel::showItem(const std::optional<ContentItem> &item)
{
    if (m_notesSaveTimer->isActive()) {
        m_notesSaveTimer->stop();
        if (m_item)
            emit notesChanged(m_item->id, m_notesEdit->toPlainText());
    }

    m_item = item;

    if (!item) {
        m_mediaPlayer->stop();
        m_mediaPlayer->setSource(QUrl());
        m_emptyState->show();
        m_content->hide();
        return;
    }

    m_emptyState->hide();
    m_content->show();

    m_titleLabel->setText(item->title.isEmpty() ? tr("Без названия") : item->title);
    m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"),
                                              QColor(item->favorite ? QStringLiteral("#f5a623") : Theme::TextDarkSecondary),
                                              20, item->favorite));

    m_notesEdit->blockSignals(true);
    m_notesEdit->setPlainText(item->notes);
    m_notesEdit->blockSignals(false);

    applyItemToPlayer();
    updateMetaLabel();
}

void VideoDetailPanel::applyItemToPlayer()
{
    const bool remote = m_item && isRemoteSource(m_item->imagePath);

    m_mediaPlayer->stop();
    if (m_item && !remote) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_item->imagePath));
    } else {
        m_mediaPlayer->setSource(QUrl());
    }

    m_videoWidget->setVisible(!remote);
    m_remoteNotice->setVisible(remote);
    m_playButton->setEnabled(!remote);
    m_timelineSlider->setEnabled(!remote);
}

void VideoDetailPanel::togglePlayback()
{
    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
        m_mediaPlayer->pause();
    else
        m_mediaPlayer->play();
}

void VideoDetailPanel::updateMetaLabel()
{
    if (!m_item) {
        m_metaLabel->clear();
        m_infoText->clear();
        return;
    }

    const bool remote = isRemoteSource(m_item->imagePath);
    QStringList parts;
    parts << tr("Видео");

    if (!remote) {
        const QSize resolution = m_mediaPlayer->metaData().value(QMediaMetaData::Resolution).toSize();
        if (resolution.isValid() && !resolution.isEmpty())
            parts << QStringLiteral("%1 × %2").arg(resolution.width()).arg(resolution.height());
        if (m_mediaPlayer->duration() > 0)
            parts << formatTime(m_mediaPlayer->duration());
        const qint64 bytes = QFileInfo(m_item->imagePath).size();
        if (bytes > 0)
            parts << tr("%1 МБ").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    } else {
        parts << QStringLiteral("YouTube");
    }
    m_metaLabel->setText(parts.join(QStringLiteral(" · ")));

    QStringList infoLines;
    infoLines << tr("Добавлено: %1").arg(m_item->createdAt.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    infoLines << (remote ? tr("Источник: ссылка YouTube") : tr("Источник: локальный файл"));
    infoLines << tr("Путь: %1").arg(m_item->imagePath);
    m_infoText->setText(infoLines.join(QStringLiteral("\n")));
}

void VideoDetailPanel::triggerGoLive()
{
    if (m_item)
        emit goLiveRequested(*m_item, m_loopMode);
}
