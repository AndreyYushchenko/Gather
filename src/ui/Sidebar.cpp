#include "Sidebar.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

// A sidebar row (icon + label + optional count badge) that can be selected.
// Deliberately a plain QWidget rather than a QPushButton: nesting QLabel
// text inside a QSS-styled QPushButton garbles ClearType text rendering on
// Windows, so rows handle their own click/selection painting instead.
class SidebarNavRow : public QFrame {
    Q_OBJECT
public:
    SidebarNavRow(QString iconName, const QString &label, bool hasBadge, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_iconName(std::move(iconName))
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 9, 10, 9);
        layout->setSpacing(10);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(17, 17);
        layout->addWidget(m_iconLabel);

        m_textLabel = new QLabel(label, this);
        layout->addWidget(m_textLabel, 1);

        if (hasBadge) {
            m_badgeLabel = new QLabel(QStringLiteral("0"), this);
            m_badgeLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(m_badgeLabel);
        }

        setSelected(false);
    }

    void setBadgeCount(int count)
    {
        if (m_badgeLabel)
            m_badgeLabel->setText(QString::number(count));
    }

    void setSelected(bool selected)
    {
        if (m_selectionApplied && m_selected == selected)
            return;
        m_selected = selected;
        m_selectionApplied = true;
        setStyleSheet(selected
                          ? QStringLiteral("SidebarNavRow { background: %1; border-radius: 8px; }").arg(Theme::AccentBlue)
                          : QStringLiteral("SidebarNavRow { background: transparent; border-radius: 8px; }"));

        const QColor fg(selected ? Theme::TextLightPrimary : Theme::TextLightSecondary);
        m_iconLabel->setPixmap(IconProvider::pixmap(m_iconName, fg, 17));
        m_textLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: %2; font-size: 13.5px;")
                                        .arg(fg.name(), selected ? QStringLiteral("600") : QStringLiteral("500")));
        if (m_badgeLabel) {
            m_badgeLabel->setStyleSheet(selected
                ? QStringLiteral("background: rgba(255,255,255,38); color: %1; border-radius: 10px; padding: 2px 7px; font-size: 11.5px; font-weight: 600;").arg(fg.name())
                : QStringLiteral("background: transparent; color: %1; border-radius: 10px; padding: 2px 7px; font-size: 11.5px; font-weight: 600;").arg(fg.name()));
        }
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }

private:
    QString m_iconName;
    bool m_selected = false;
    bool m_selectionApplied = false;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QLabel *m_badgeLabel = nullptr;
};

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Sidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 248 is this panel's *maximum* width, not fixed — see
    // DisplayControlPanel's matching comment for why.
    setMinimumWidth(200);
    setMaximumWidth(248);
    setStyleSheet(QStringLiteral("QWidget#Sidebar { background: %1; }").arg(Theme::BgDark));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(0);

    // Logo row
    auto *logoRow = new QHBoxLayout;
    logoRow->setSpacing(10);
    auto *logoBox = new QLabel;
    logoBox->setFixedSize(38, 38);
    logoBox->setAlignment(Qt::AlignCenter);
    // design.pen fills the 38×38 box with design-assets/app-icon-trimmed.png,
    // the same artwork as the window/taskbar icon.
    logoBox->setPixmap(QIcon(QStringLiteral(":/icons/app-256.png")).pixmap(38, 38));
    logoRow->addWidget(logoBox);

    auto *logoTextCol = new QVBoxLayout;
    logoTextCol->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("Sermon"));
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 700; font-size: 18px;").arg(Theme::TextLightPrimary));
    auto *subtitle = new QLabel(tr("Церковь и общество"));
    subtitle->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(Theme::TextLightSecondary));
    logoTextCol->addWidget(title);
    logoTextCol->addWidget(subtitle);
    logoRow->addLayout(logoTextCol);
    logoRow->addStretch();
    layout->addLayout(logoRow);
    layout->addSpacing(18);

    // Add button (native icon+text rendering; no nested QLabel, so unaffected
    // by the QPushButton/QSS text-garbling issue above)
    auto *addButton = new QPushButton(tr("  Добавить"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setIcon(IconProvider::icon(QStringLiteral("plus"), QColor(Theme::TextLightPrimary), 16));
    addButton->setIconSize(QSize(16, 16));
    addButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 9px; "
        "padding: 10px 14px; font-weight: 600; font-size: 14px; text-align: center; }"
        "QPushButton:hover { background: #255ed1; }")
            .arg(Theme::AccentBlue, Theme::TextLightPrimary));
    connect(addButton, &QPushButton::clicked, this, &Sidebar::addRequested);
    layout->addWidget(addButton);
    layout->addSpacing(18);

    // design.pen's Nav List: Песни/Библия/Объявления/Фото/Видео/Плейлисты
    // in one continuous list, gap 2, no divider. Only Библия has no count badge.
    auto *navList = new QVBoxLayout;
    navList->setSpacing(2);

    // Category labels here are the sidebar's own (plural/section) wording
    // per design.pen, not contentTypeDisplayName()'s singular per-item type
    // name (e.g. "Библия" the section vs "Стих из Библии" an item's type).
    struct Entry { ContentType type; QString icon; QString label; bool badge; };
    const Entry entries[] = {
        {ContentType::Song, QStringLiteral("music"), tr("Песни"), true},
        {ContentType::BibleVerse, QStringLiteral("book-open"), tr("Библия"), false},
        {ContentType::Announcement, QStringLiteral("megaphone"), tr("Объявления"), true},
        {ContentType::Photo, QStringLiteral("image"), tr("Фото"), true},
    };

    for (const Entry &entry : entries) {
        auto *row = new SidebarNavRow(entry.icon, entry.label, entry.badge, this);
        connect(row, &SidebarNavRow::clicked, this, [this, type = entry.type]() { selectCategory(type); });
        navList->addWidget(row);
        m_categoryRows.append({row, entry.type});
        m_allRows.append(row);
    }

    auto *video = new SidebarNavRow(QStringLiteral("video"), tr("Видео"), true, this);
    m_videoRow = video;
    connect(video, &SidebarNavRow::clicked, this, [this, video]() { activateRow(video); emit videoRequested(); });
    navList->addWidget(video);
    m_allRows.append(video);

    auto *playlists = new SidebarNavRow(QStringLiteral("list-music"), tr("Плейлисты"), true, this);
    connect(playlists, &SidebarNavRow::clicked, this, [this, playlists]() { activateRow(playlists); emit playlistsRequested(); });
    navList->addWidget(playlists);
    m_allRows.append(playlists);
    m_playlistRow = playlists;

    auto *timers = new SidebarNavRow(QStringLiteral("timer"), tr("Таймеры"), false, this);
    connect(timers, &SidebarNavRow::clicked, this, [this, timers]() { activateRow(timers); emit timersRequested(); });
    m_timersRow = timers;
    navList->addWidget(timers);
    m_allRows.append(timers);
    layout->addLayout(navList);

    layout->addStretch();

    auto *bottomList = new QVBoxLayout;
    bottomList->setSpacing(2);
    auto *importExport = new SidebarNavRow(QStringLiteral("arrow-left-right"), tr("Импорт / Экспорт"), false, this);
    connect(importExport, &SidebarNavRow::clicked, this, [this, importExport]() { activateRow(importExport); emit importExportRequested(); });
    bottomList->addWidget(importExport);
    m_importExportRow = importExport;
    m_allRows.append(importExport);

    auto *settings = new SidebarNavRow(QStringLiteral("settings"), tr("Настройки"), false, this);
    connect(settings, &SidebarNavRow::clicked, this, [this, settings]() { activateRow(settings); emit settingsRequested(); });
    bottomList->addWidget(settings);
    m_settingsRow = settings;
    m_allRows.append(settings);

    auto *help = new SidebarNavRow(QStringLiteral("info"), tr("Справка"), false, this);
    connect(help, &SidebarNavRow::clicked, this, [this, help]() { activateRow(help); emit helpRequested(); });
    bottomList->addWidget(help);
    m_helpRow = help;
    m_allRows.append(help);
    layout->addLayout(bottomList);

    selectCategory(ContentType::Song);
}

void Sidebar::activateRow(SidebarNavRow *active)
{
    for (SidebarNavRow *row : std::as_const(m_allRows))
        row->setSelected(row == active);
}

void Sidebar::selectCategory(ContentType type)
{
    m_selectedCategory = type;
    for (const CategoryRow &row : std::as_const(m_categoryRows)) {
        if (row.type == type)
            activateRow(row.row);
    }
    emit categorySelected(type);
}

void Sidebar::setCounts(const QMap<ContentType, int> &counts)
{
    for (const CategoryRow &row : std::as_const(m_categoryRows))
        row.row->setBadgeCount(counts.value(row.type, 0));
    // Видео isn't a library category row (it opens its own screen), so its
    // badge needs setting separately.
    m_videoRow->setBadgeCount(counts.value(ContentType::Video, 0));
}

void Sidebar::openSection(const QString &key)
{
    if (key == QLatin1String("bible"))
        selectCategory(ContentType::BibleVerse);
    else if (key == QLatin1String("announcements"))
        selectCategory(ContentType::Announcement);
    else if (key == QLatin1String("photos"))
        selectCategory(ContentType::Photo);
    else if (key == QLatin1String("videos")) {
        activateRow(m_videoRow);
        emit videoRequested();
    } else if (key == QLatin1String("playlists")) {
        activateRow(m_playlistRow);
        emit playlistsRequested();
    } else if (key == QLatin1String("timers")) {
        activateRow(m_timersRow);
        emit timersRequested();
    } else if (key == QLatin1String("settings")) {
        activateRow(m_settingsRow);
        emit settingsRequested();
    } else if (key == QLatin1String("help")) {
        activateRow(m_helpRow);
        emit helpRequested();
    } else if (key == QLatin1String("importexport")) {
        activateRow(m_importExportRow);
        emit importExportRequested();
    } else {
        selectCategory(ContentType::Song);
    }
}

void Sidebar::setPlaylistCount(int count)
{
    m_playlistRow->setBadgeCount(count);
}

#include "Sidebar.moc"
