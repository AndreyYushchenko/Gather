#include "Sidebar.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace {
QWidget *divider(QWidget *parent)
{
    auto *line = new QWidget(parent);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderDark));
    return line;
}
}

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
        // Scoped to SidebarNavRow: an unscoped rule would cascade its
        // border-radius down onto the child QLabels too.
        setStyleSheet(selected
                          ? QStringLiteral("SidebarNavRow { background: %1; border-radius: 8px; }").arg(Theme::AccentBlue)
                          : QStringLiteral("SidebarNavRow { background: transparent; border-radius: 8px; }"));

        const QColor fg(selected ? Theme::TextLightPrimary : Theme::TextLightSecondary);
        m_iconLabel->setPixmap(IconProvider::pixmap(m_iconName, fg, 17));
        m_textLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: %2; font-size: 13.5px;")
                                        .arg(fg.name(), selected ? QStringLiteral("600") : QStringLiteral("500")));
        if (m_badgeLabel) {
            m_badgeLabel->setStyleSheet(QStringLiteral(
                "background: rgba(255,255,255,%1); color: %2; border-radius: 10px; "
                "padding: 2px 7px; font-size: 11.5px; font-weight: 600;")
                    .arg(selected ? QStringLiteral("0.20") : QStringLiteral("0.10"), fg.name()));
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
    setFixedWidth(248);
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
    logoBox->setPixmap(IconProvider::pixmap(QStringLiteral("church"), QColor(Theme::TextLightPrimary), 20));
    logoBox->setStyleSheet(QStringLiteral("background: %1; border-radius: 9px;").arg(Theme::AccentBlue));
    logoRow->addWidget(logoBox);

    auto *logoTextCol = new QVBoxLayout;
    logoTextCol->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("Gather"));
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 700; font-size: 18px;").arg(Theme::TextLightPrimary));
    auto *subtitle = new QLabel(QStringLiteral("Церковные презентации"));
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

    // Category nav
    struct Entry { ContentType type; QString icon; };
    const Entry entries[] = {
        {ContentType::Song, QStringLiteral("music")},
        {ContentType::BibleVerse, QStringLiteral("book-open")},
        {ContentType::Announcement, QStringLiteral("megaphone")},
        {ContentType::Photo, QStringLiteral("image")},
    };

    for (const Entry &entry : entries) {
        auto *row = new SidebarNavRow(entry.icon, contentTypeDisplayName(entry.type), true, this);
        connect(row, &SidebarNavRow::clicked, this, [this, type = entry.type]() { selectCategory(type); });
        layout->addWidget(row);
        m_categoryRows.append({row, entry.type});
    }

    layout->addSpacing(14);
    layout->addWidget(divider(this));
    layout->addSpacing(14);

    auto *playlists = new SidebarNavRow(QStringLiteral("list-music"), tr("Плейлисты"), true, this);
    connect(playlists, &SidebarNavRow::clicked, this, &Sidebar::playlistsRequested);
    layout->addWidget(playlists);

    auto *video = new SidebarNavRow(QStringLiteral("video"), tr("Видео"), false, this);
    connect(video, &SidebarNavRow::clicked, this, &Sidebar::videoRequested);
    layout->addWidget(video);

    layout->addStretch();

    auto *importExport = new SidebarNavRow(QStringLiteral("arrow-left-right"), tr("Импорт / Экспорт"), false, this);
    connect(importExport, &SidebarNavRow::clicked, this, &Sidebar::importExportRequested);
    layout->addWidget(importExport);

    auto *settings = new SidebarNavRow(QStringLiteral("settings"), tr("Настройки"), false, this);
    connect(settings, &SidebarNavRow::clicked, this, &Sidebar::settingsRequested);
    layout->addWidget(settings);

    auto *help = new SidebarNavRow(QStringLiteral("info"), tr("Справка"), false, this);
    connect(help, &SidebarNavRow::clicked, this, &Sidebar::helpRequested);
    layout->addWidget(help);

    selectCategory(ContentType::Song);
}

void Sidebar::selectCategory(ContentType type)
{
    m_selectedCategory = type;
    for (const CategoryRow &row : std::as_const(m_categoryRows))
        row.row->setSelected(row.type == type);
    emit categorySelected(type);
}

void Sidebar::setCounts(const QMap<ContentType, int> &counts)
{
    for (const CategoryRow &row : std::as_const(m_categoryRows))
        row.row->setBadgeCount(counts.value(row.type, 0));
}

#include "Sidebar.moc"
