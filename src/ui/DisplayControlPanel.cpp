#include "DisplayControlPanel.h"
#include "IconProvider.h"
#include "QuickListCard.h"
#include "Theme.h"
#include "display/DisplayServer.h"
#include "display/PresentationController.h"
#include "display/SlideRenderWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

// A toggleable "icon + label ... shortcut key" row (Black screen / Pause).
// Plain QFrame rather than QPushButton: nesting QLabel text inside a
// QSS-styled QPushButton garbles ClearType text rendering on Windows.
class ShortcutRow : public QFrame {
    Q_OBJECT
public:
    ShortcutRow(const QString &iconName, const QString &label, const QString &shortcut, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_iconName(iconName)
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 11, 14, 11);
        layout->setSpacing(8);

        m_iconLabel = new QLabel(this);
        layout->addWidget(m_iconLabel);

        m_textLabel = new QLabel(label, this);
        m_textLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; font-size: 13.5px; background: transparent; border: none;")
                                        .arg(Theme::TextLightPrimary));
        layout->addWidget(m_textLabel);

        layout->addStretch();

        m_shortcutLabel = new QLabel(shortcut, this);
        m_shortcutLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 500; font-size: 12px; background: transparent;")
                                          .arg(Theme::TextLightSecondary));
        layout->addWidget(m_shortcutLabel);

        setChecked(false);
    }

    bool isChecked() const { return m_checked; }
    void setShortcut(const QString &shortcut) { m_shortcutLabel->setText(shortcut); }

    void setChecked(bool checked)
    {
        m_checked = checked;
        // Scoped to ShortcutRow specifically: a bare (unscoped) rule here would
        // otherwise cascade its border/border-radius down onto the child
        // QLabels too.
        setStyleSheet(checked
            ? QStringLiteral("ShortcutRow { background: %1; border: 1px solid %1; border-radius: 9px; }").arg(Theme::AccentBlue)
            : QStringLiteral("ShortcutRow { background: transparent; border: 1px solid %1; border-radius: 9px; }").arg(Theme::BorderDark));
        m_iconLabel->setPixmap(IconProvider::pixmap(m_iconName, QColor(Theme::TextLightPrimary), 15));
    }

signals:
    void toggled(bool checked);

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        setChecked(!m_checked);
        emit toggled(m_checked);
    }

private:
    QString m_iconName;
    bool m_checked = false;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QLabel *m_shortcutLabel = nullptr;
};

DisplayControlPanel::DisplayControlPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DisplayControlPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 430 (design.pen node HsDXK) is this panel's *maximum* width, not a
    // fixed one — it's on every screen, so it was the single biggest
    // contributor to the app having nowhere to shrink below ~1680px wide.
    // Its own content (buttons, OBS field, mini preview) is already
    // naturally flexible internally; only the outer width was hard-fixed.
    setMinimumWidth(340);
    setMaximumWidth(430);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);


    // ---- Header ----
    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);
    auto *title = new QLabel(tr("Окно показа (проектор)"));
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 700; font-size: 15px;").arg(Theme::TextLightPrimary));
    headerRow->addWidget(title);
    headerRow->addStretch();

    m_statusDot = makeStatusDot();
    m_statusLabel = new QLabel(tr("Не открыто"));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600;"));
    headerRow->addWidget(m_statusDot);
    headerRow->addWidget(m_statusLabel);

    auto *expandButton = new QPushButton;
    expandButton->setFlat(true);
    expandButton->setCursor(Qt::PointingHandCursor);
    expandButton->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; padding: 2px; }"));
    expandButton->setIcon(IconProvider::icon(QStringLiteral("external-link"), QColor(Theme::TextLightSecondary), 15));
    expandButton->setIconSize(QSize(15, 15));
    expandButton->setToolTip(tr("Показать окно показа поверх остальных окон"));
    connect(expandButton, &QPushButton::clicked, this, [this]() {
        if (!m_controller || !m_controller->isDisplayWindowVisible()) {
            QMessageBox::information(this, tr("Окно показа"),
                                      tr("Окно ещё не открыто. Нажмите «На экран» на любой записи, чтобы открыть его."));
            return;
        }
        m_controller->raiseDisplayWindow();
    });
    headerRow->addWidget(expandButton);
    layout->addLayout(headerRow);

    // ---- Mini preview ----
    m_miniPreview = new SlideRenderWidget;
    m_miniPreview->setFixedHeight(230);
    m_miniPreview->setStyleSheet(QStringLiteral("SlideRenderWidget { border-radius: 12px; background: %1; }").arg(Theme::BgDark2));

    m_slideCounterOverlay = new QLabel(m_miniPreview);
    m_slideCounterOverlay->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600; background: transparent;")
                                              .arg(Theme::TextLightSecondary));
    m_slideCounterOverlay->hide();

    layout->addWidget(m_miniPreview);
    m_miniPreview->setAlwaysMuted(true);
    m_miniPreview->setPreviewMode(true);

    // ---- Playlist position (design.pen "Now Next Box": fill #ffffff0d,
    // radius 10, padding [12, 14], gap 8; label column 62 wide). ----
    m_playlistBox = new QFrame;
    m_playlistBox->setObjectName(QStringLiteral("PlaylistBox"));
    m_playlistBox->setStyleSheet(QStringLiteral("QFrame#PlaylistBox { background: rgba(255, 255, 255, 13); border-radius: 10px; }"));
    auto *playlistLayout = new QGridLayout(m_playlistBox);
    playlistLayout->setContentsMargins(14, 12, 14, 12);
    playlistLayout->setHorizontalSpacing(10);
    playlistLayout->setVerticalSpacing(8);
    const auto boxLabel = [](const QString &textValue) {
        auto *label = new QLabel(textValue);
        label->setFixedWidth(62);
        label->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 13px; font-weight: 500;")
                                 .arg(Theme::TextLightSecondary));
        return label;
    };
    m_nowLabel = new QLabel;
    m_nowLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 13px; font-weight: 700;").arg(Theme::AccentBlue));
    m_nextLabel = new QLabel;
    m_nextLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 13px; font-weight: 500;").arg(Theme::TextLightPrimary));
    for (QLabel *label : {m_nowLabel, m_nextLabel})
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    playlistLayout->addWidget(boxLabel(tr("Сейчас:")), 0, 0);
    playlistLayout->addWidget(m_nowLabel, 0, 1);
    playlistLayout->addWidget(boxLabel(tr("Далее:")), 1, 0);
    playlistLayout->addWidget(m_nextLabel, 1, 1);
    playlistLayout->setColumnStretch(1, 1);
    m_playlistBox->hide();
    layout->addWidget(m_playlistBox);

    // ---- Live video transport (design.pen "Player Controls"): shown only
    // while a local video is on the projector; drives the projector's player
    // (and keeps the silent mini preview in step). ----
    m_videoControls = new QWidget;
    auto *videoLayout = new QVBoxLayout(m_videoControls);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(6);
    m_videoSlider = new QSlider(Qt::Horizontal);
    m_videoSlider->setRange(0, 1000);
    m_videoSlider->setCursor(Qt::PointingHandCursor);
    m_videoSlider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,64); border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %1; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #ffffff; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }")
                                     .arg(Theme::AccentBlue));
    connect(m_videoSlider, &QSlider::sliderReleased, this, [this]() {
        QMediaPlayer *player = m_controller ? m_controller->liveVideoPlayer() : nullptr;
        if (!player || player->duration() <= 0)
            return;
        const qint64 position = player->duration() * m_videoSlider->value() / 1000;
        player->setPosition(position);
        if (QMediaPlayer *mini = m_miniPreview->mediaPlayer())
            mini->setPosition(position);
    });
    videoLayout->addWidget(m_videoSlider);

    const auto transportButton = [](const QString &tooltip) {
        auto *button = new QPushButton;
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setIconSize(QSize(20, 20));
        button->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; padding: 2px; }"));
        return button;
    };
    auto *transportRow = new QHBoxLayout;
    transportRow->setSpacing(12);
    m_videoPlayButton = transportButton(tr("Пауза / продолжить"));
    connect(m_videoPlayButton, &QPushButton::clicked, this, [this]() {
        QMediaPlayer *player = m_controller ? m_controller->liveVideoPlayer() : nullptr;
        if (!player)
            return;
        const bool playing = player->playbackState() == QMediaPlayer::PlayingState;
        for (QMediaPlayer *p : {player, m_miniPreview->mediaPlayer()}) {
            if (!p)
                continue;
            if (playing)
                p->pause();
            else
                p->play();
        }
        updateVideoControls();
    });
    transportRow->addWidget(m_videoPlayButton);
    m_videoTimeLabel = new QLabel;
    m_videoTimeLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 14px; font-weight: 500;").arg(Theme::TextLightPrimary));
    transportRow->addWidget(m_videoTimeLabel);
    transportRow->addStretch();
    m_videoMuteButton = transportButton(tr("Звук на проекторе вкл/выкл"));
    connect(m_videoMuteButton, &QPushButton::clicked, this, [this]() {
        if (QAudioOutput *audio = m_controller ? m_controller->liveAudioOutput() : nullptr)
            audio->setMuted(!audio->isMuted());
        updateVideoControls();
    });
    transportRow->addWidget(m_videoMuteButton);
    auto *fullScreen = transportButton(tr("Окно проектора: во весь экран / в окне"));
    fullScreen->setIcon(IconProvider::icon(QStringLiteral("maximize"), QColor(Theme::TextLightPrimary), 20));
    connect(fullScreen, &QPushButton::clicked, this, [this]() {
        if (m_controller)
            m_controller->toggleDisplayFullScreen();
    });
    transportRow->addWidget(fullScreen);
    videoLayout->addLayout(transportRow);
    m_videoControls->hide();
    layout->addWidget(m_videoControls);

    auto *videoTick = new QTimer(this);
    videoTick->setInterval(250);
    connect(videoTick, &QTimer::timeout, this, &DisplayControlPanel::updateVideoControls);
    videoTick->start();

    // ---- Prev/next ----
    auto *navRow = new QHBoxLayout;
    navRow->setSpacing(12);
    m_backButton = new QPushButton;
    m_backButton->setObjectName(QStringLiteral("DarkOutlineButton"));
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setIcon(IconProvider::icon(QStringLiteral("arrow-left"), QColor(Theme::TextLightPrimary), 15));
    m_backButton->setIconSize(QSize(15, 15));
    m_backButton->setText(tr("Назад"));

    m_forwardButton = new QPushButton;
    m_forwardButton->setObjectName(QStringLiteral("DarkOutlineButton"));
    m_forwardButton->setCursor(Qt::PointingHandCursor);
    m_forwardButton->setLayoutDirection(Qt::RightToLeft);
    m_forwardButton->setIcon(IconProvider::icon(QStringLiteral("arrow-right"), QColor(Theme::TextLightPrimary), 15));
    m_forwardButton->setIconSize(QSize(15, 15));
    m_forwardButton->setText(tr("Вперёд"));

    navRow->addWidget(m_backButton);
    navRow->addWidget(m_forwardButton);
    layout->addLayout(navRow);

    // ---- Hide / pause ----
    auto *controlRow = new QHBoxLayout;
    controlRow->setSpacing(12);
    m_blackButton = makeShortcutRow(QStringLiteral("monitor-off"), tr("Скрыть"), QStringLiteral("Esc"));
    m_pauseButton = makeShortcutRow(QStringLiteral("pause"), tr("Пауза"), QStringLiteral("Space"));
    controlRow->addWidget(m_blackButton);
    controlRow->addWidget(m_pauseButton);
    layout->addLayout(controlRow);

    // ---- OBS ----
    auto *obsHeaderRow = new QHBoxLayout;
    obsHeaderRow->setSpacing(6);
    auto *obsLabel = new QLabel(tr("Вывод на OBS (веб-адрес)"));
    obsLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; font-size: 13.5px;").arg(Theme::TextLightPrimary));
    obsHeaderRow->addWidget(obsLabel);
    obsHeaderRow->addStretch();
    m_obsStatusDot = makeStatusDot();
    m_obsStatusLabel = new QLabel;
    m_obsStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600;"));
    obsHeaderRow->addWidget(m_obsStatusDot);
    obsHeaderRow->addWidget(m_obsStatusLabel);
    layout->addLayout(obsHeaderRow);

    auto *obsUrlRow = new QHBoxLayout;
    obsUrlRow->setSpacing(10);
    m_obsUrlLabel = new QLabel;
    m_obsUrlLabel->setObjectName(QStringLiteral("ObsUrlField"));
    obsUrlRow->addWidget(m_obsUrlLabel, 1);

    auto *copyButton = new QPushButton;
    copyButton->setObjectName(QStringLiteral("DarkOutlineIconButton"));
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setFixedSize(38, 38);
    copyButton->setIcon(IconProvider::icon(QStringLiteral("copy"), QColor(Theme::TextLightPrimary), 15));
    copyButton->setIconSize(QSize(15, 15));
    copyButton->setToolTip(tr("Скопировать адрес"));
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_obsUrlLabel->text());
    });
    obsUrlRow->addWidget(copyButton);

    auto *qrButton = new QPushButton;
    qrButton->setObjectName(QStringLiteral("DarkOutlineIconButton"));
    qrButton->setCursor(Qt::PointingHandCursor);
    qrButton->setFixedSize(38, 38);
    qrButton->setIcon(IconProvider::icon(QStringLiteral("qr-code"), QColor(Theme::TextLightPrimary), 15));
    qrButton->setIconSize(QSize(15, 15));
    qrButton->setToolTip(tr("Показать адрес"));
    connect(qrButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Адрес для OBS"),
                                  tr("QR-код пока не реализован. Адрес для ручного ввода:\n%1").arg(m_obsUrlLabel->text()));
    });
    obsUrlRow->addWidget(qrButton);
    layout->addLayout(obsUrlRow);

    auto *obsHelper = new QLabel(tr("Откройте этот адрес в OBS (источник «Браузер»). "
                                     "Оба устройства должны быть в одной локальной сети."));
    obsHelper->setWordWrap(true);
    obsHelper->setStyleSheet(QStringLiteral("color: %1; font-size: 11.5px;").arg(Theme::TextLightSecondary));
    layout->addWidget(obsHelper);

    // ---- Quick list ----
    m_quickList = new QuickListCard;
    connect(m_quickList, &QuickListCard::goLiveRequested, this, [this](const ContentItem &item, int slideIndex) {
        if (m_controller)
            m_controller->goLive(item, slideIndex);
    });
    connect(m_quickList, &QuickListCard::addCurrentSongRequested, this, &DisplayControlPanel::addCurrentSongRequested);
    layout->addWidget(m_quickList);

    layout->addStretch();

    auto *footerDivider = new QWidget;
    footerDivider->setFixedHeight(1);
    footerDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderDark));
    layout->addWidget(footerDivider);

    auto *footerRow = new QHBoxLayout;
    footerRow->setSpacing(0);
    auto *readyDot = makeStatusDot();
    readyDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(Theme::AccentGreen));
    auto *readyLabel = new QLabel(tr("Готов к показу"));
    readyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextLightSecondary));
    footerRow->addWidget(readyDot);
    footerRow->addSpacing(6);
    footerRow->addWidget(readyLabel);
    footerRow->addStretch();
    auto *versionLabel = new QLabel(tr("Sermon v1.0.0"));
    versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Theme::TextLightSecondary));
    footerRow->addWidget(versionLabel);
    footerRow->addSpacing(8);
    auto *footerSettingsIcon = new QLabel;
    footerSettingsIcon->setPixmap(IconProvider::pixmap(QStringLiteral("settings"), QColor(Theme::TextLightSecondary), 14));
    footerRow->addWidget(footerSettingsIcon);
    layout->addLayout(footerRow);

    connect(m_backButton, &QPushButton::clicked, this, [this]() { if (m_controller) m_controller->stepBack(); });
    connect(m_forwardButton, &QPushButton::clicked, this, [this]() { if (m_controller) m_controller->stepForward(); });
    connect(m_blackButton, &ShortcutRow::toggled, this, [this](bool) { 
        if (m_controller) m_controller->endShow(); 
        m_blackButton->setChecked(false);
    });
    connect(m_pauseButton, &ShortcutRow::toggled, this, [this](bool) { if (m_controller) m_controller->toggleFrozen(); });

    setStyleSheet(QStringLiteral(R"(
        QWidget#DisplayControlPanel { background: %1; }
        QPushButton#DarkOutlineButton, QPushButton#DarkOutlineIconButton {
            background: transparent; border: 1px solid %2; border-radius: 9px;
            padding: 11px 14px; color: %3; font-weight: 600; font-size: 13.5px;
        }
        QPushButton#DarkOutlineIconButton { padding: 9px; }
        QPushButton#DarkOutlineButton:hover, QPushButton#DarkOutlineIconButton:hover { background: %4; }
        QLabel#ObsUrlField {
            background: %4; border: 1px solid %2; border-radius: 9px;
            padding: 11px 14px; color: %6; font-size: 13px;
        }
    )").arg(Theme::BgDark, Theme::BorderDark, Theme::TextLightPrimary, Theme::BgDark2, Theme::AccentBlue, Theme::TextLightSecondary));

    updateStatus();
}

void DisplayControlPanel::addSongToQuickList(const ContentItem &item)
{
    m_quickList->addItem(item);
}

void DisplayControlPanel::setShortcutLabels(const QString &hide, const QString &pause)
{
    m_blackButton->setShortcut(hide);
    m_pauseButton->setShortcut(pause);
}

void DisplayControlPanel::setQuickListVisible(bool visible)
{
    m_quickList->setVisible(visible);
}

void DisplayControlPanel::updateVideoControls()
{
    QMediaPlayer *player = m_controller ? m_controller->liveVideoPlayer() : nullptr;
    const bool show = m_liveLocalVideo && player;
    m_videoControls->setVisible(show);
    if (!show)
        return;
    const auto clock = [](qint64 ms) {
        const qint64 s = qMax<qint64>(0, ms) / 1000;
        return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    };
    const qint64 duration = player->duration();
    m_videoTimeLabel->setText(QStringLiteral("%1 / %2").arg(clock(player->position()), clock(duration)));
    if (!m_videoSlider->isSliderDown() && duration > 0)
        m_videoSlider->setValue(int(player->position() * 1000 / duration));
    const bool playing = player->playbackState() == QMediaPlayer::PlayingState;
    if (m_shownPlaying != int(playing)) {
        m_shownPlaying = int(playing);
        m_videoPlayButton->setIcon(IconProvider::icon(playing ? QStringLiteral("pause") : QStringLiteral("play"),
                                                      QColor(Theme::TextLightPrimary), 20));
    }
    QAudioOutput *audio = m_controller->liveAudioOutput();
    const bool muted = audio && audio->isMuted();
    if (m_shownMuted != int(muted)) {
        m_shownMuted = int(muted);
        m_videoMuteButton->setIcon(IconProvider::icon(muted ? QStringLiteral("volume-x") : QStringLiteral("volume-2"),
                                                      QColor(Theme::TextLightPrimary), 20));
    }
}

QLabel *DisplayControlPanel::makeStatusDot()
{
    auto *dot = new QLabel;
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(Theme::TextLightSecondary));
    return dot;
}

ShortcutRow *DisplayControlPanel::makeShortcutRow(const QString &iconName, const QString &label, const QString &shortcut)
{
    return new ShortcutRow(iconName, label, shortcut, this);
}

void DisplayControlPanel::setController(PresentationController *controller)
{
    m_controller = controller;

    connect(controller, &PresentationController::contentChanged, this, [this](const SlideContent &content) {
        m_miniPreview->setContent(content);
        m_liveLocalVideo = content.kind == SlideKind::Video && !content.imagePath.startsWith(QStringLiteral("http://"))
            && !content.imagePath.startsWith(QStringLiteral("https://"));
        updateVideoControls();
    });

    connect(controller, &PresentationController::frozenChanged, this, [this](bool frozen) {
        m_pauseButton->setChecked(frozen);
    });
    connect(controller, &PresentationController::liveSlideChanged, this, [this](int index, int count) {
        if (count > 1) {
            m_slideCounterOverlay->setText(tr("%1 / %2").arg(index + 1).arg(count));
            m_slideCounterOverlay->adjustSize();
            m_slideCounterOverlay->show();
            positionSlideCounter();
        } else {
            m_slideCounterOverlay->hide();
        }
        updateStatus();
    });
    connect(controller, &PresentationController::displayWindowVisibilityChanged, this, [this](bool) {
        updateStatus();
    });
    connect(controller, &PresentationController::playlistPositionChanged, this, &DisplayControlPanel::updatePlaylistBox);

    m_obsUrlLabel->setText(controller->server()->displayUrl());
    updateStatus();
}

void DisplayControlPanel::updatePlaylistBox()
{
    if (!m_controller || !m_controller->isPlaylistLive()) {
        m_playlistBox->hide();
        return;
    }
    const QList<ContentItem> &items = m_controller->livePlaylist();
    const int index = m_controller->livePlaylistIndex();
    const auto entryText = [&items](int i) {
        return QStringLiteral("%1. %2").arg(i + 1).arg(items.at(i).displayTitle());
    };
    m_nowLabel->setText(entryText(index));
    m_nowLabel->setToolTip(m_nowLabel->text());
    m_nextLabel->setText(index + 1 < items.size() ? entryText(index + 1) : tr("Конец плейлиста"));
    m_nextLabel->setToolTip(m_nextLabel->text());
    m_playlistBox->show();
}

void DisplayControlPanel::positionSlideCounter()
{
    m_slideCounterOverlay->move(m_miniPreview->width() - m_slideCounterOverlay->width() - 24,
                                 m_miniPreview->height() - m_slideCounterOverlay->height() - 24);
}

void DisplayControlPanel::updateStatus()
{
    const bool windowVisible = m_controller && m_controller->isDisplayWindowVisible();
    const bool serverRunning = m_controller && m_controller->server() && m_controller->server()->isRunning();
    if (m_shownWindowVisible == int(windowVisible) && m_shownServerRunning == int(serverRunning))
        return;
    m_shownWindowVisible = int(windowVisible);
    m_shownServerRunning = int(serverRunning);
    m_statusDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: %1;")
                                    .arg(windowVisible ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1;")
                                      .arg(windowVisible ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_statusLabel->setText(windowVisible ? tr("Активно") : tr("Не открыто"));

    m_obsStatusDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: %1;")
                                       .arg(serverRunning ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_obsStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1;")
                                         .arg(serverRunning ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_obsStatusLabel->setText(serverRunning ? tr("Запущен") : tr("Остановлен"));
}

#include "DisplayControlPanel.moc"
