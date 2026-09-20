#include "DisplayControlPanel.h"
#include "IconProvider.h"
#include "Theme.h"
#include "display/DisplayServer.h"
#include "display/PresentationController.h"
#include "display/SlideRenderWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
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

        auto *shortcutLabel = new QLabel(shortcut, this);
        shortcutLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 500; font-size: 12px; background: transparent;")
                                          .arg(Theme::TextLightSecondary));
        layout->addWidget(shortcutLabel);

        setChecked(false);
    }

    bool isChecked() const { return m_checked; }

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
};

DisplayControlPanel::DisplayControlPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DisplayControlPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(430);

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
    m_miniPreview->setStyleSheet(QStringLiteral("SlideRenderWidget { border-radius: 12px; background: #0b0e14; }"));

    m_slideCounterOverlay = new QLabel(m_miniPreview);
    m_slideCounterOverlay->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600; background: transparent;")
                                              .arg(Theme::TextLightSecondary));
    m_slideCounterOverlay->hide();

    layout->addWidget(m_miniPreview);

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

    // ---- Black / pause ----
    auto *controlRow = new QHBoxLayout;
    controlRow->setSpacing(12);
    m_blackButton = makeShortcutRow(QStringLiteral("monitor-off"), tr("Чёрный экран"), QStringLiteral("B"));
    m_pauseButton = makeShortcutRow(QStringLiteral("pause"), tr("Пауза"), QStringLiteral("Space"));
    controlRow->addWidget(m_blackButton);
    controlRow->addWidget(m_pauseButton);
    layout->addLayout(controlRow);

    // ---- OBS ----
    auto *obsHeaderRow = new QHBoxLayout;
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

    layout->addStretch();

    auto *footerDivider = new QWidget;
    footerDivider->setFixedHeight(1);
    footerDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderDark));
    layout->addWidget(footerDivider);

    auto *footerRow = new QHBoxLayout;
    footerRow->setSpacing(6);
    auto *readyDot = makeStatusDot();
    readyDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(Theme::AccentGreen));
    auto *readyLabel = new QLabel(tr("Готово к показу"));
    readyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextLightSecondary));
    footerRow->addWidget(readyDot);
    footerRow->addWidget(readyLabel);
    footerRow->addStretch();
    auto *versionLabel = new QLabel(tr("Gather v1.0.0"));
    versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Theme::TextLightSecondary));
    footerRow->addWidget(versionLabel);
    auto *footerSettingsIcon = new QLabel;
    footerSettingsIcon->setPixmap(IconProvider::pixmap(QStringLiteral("settings"), QColor(Theme::TextLightSecondary), 14));
    footerRow->addWidget(footerSettingsIcon);
    layout->addLayout(footerRow);

    connect(m_backButton, &QPushButton::clicked, this, [this]() { if (m_controller) m_controller->stepBack(); });
    connect(m_forwardButton, &QPushButton::clicked, this, [this]() { if (m_controller) m_controller->stepForward(); });
    connect(m_blackButton, &ShortcutRow::toggled, this, [this](bool on) { if (m_controller) m_controller->setBlack(on); });
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
    });
    connect(controller, &PresentationController::blackChanged, this, [this](bool black) {
        m_blackButton->setChecked(black);
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

    m_obsUrlLabel->setText(controller->server()->displayUrl());
    updateStatus();
}

void DisplayControlPanel::positionSlideCounter()
{
    m_slideCounterOverlay->move(m_miniPreview->width() - m_slideCounterOverlay->width() - 16,
                                 m_miniPreview->height() - m_slideCounterOverlay->height() - 16);
}

void DisplayControlPanel::updateStatus()
{
    const bool windowVisible = m_controller && m_controller->isDisplayWindowVisible();
    m_statusDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: %1;")
                                    .arg(windowVisible ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1;")
                                      .arg(windowVisible ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_statusLabel->setText(windowVisible ? tr("Активно") : tr("Не открыто"));

    const bool serverRunning = m_controller && m_controller->server() && m_controller->server()->isRunning();
    m_obsStatusDot->setStyleSheet(QStringLiteral("border-radius: 4px; background: %1;")
                                       .arg(serverRunning ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_obsStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1;")
                                         .arg(serverRunning ? Theme::AccentGreen : Theme::TextLightSecondary));
    m_obsStatusLabel->setText(serverRunning ? tr("Запущен") : tr("Остановлен"));
}

#include "DisplayControlPanel.moc"
