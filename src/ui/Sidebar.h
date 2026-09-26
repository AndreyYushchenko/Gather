#pragma once

#include "core/ContentItem.h"

#include <QMap>
#include <QWidget>

class SidebarNavRow;

// Dark left rail: logo, "Add" button, category navigation with live counts,
// and the bottom utility items (import/export, settings, help).
class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);

    void setCounts(const QMap<ContentType, int> &counts);
    void setPlaylistCount(int count);
    // Opens a section as if its row was clicked: "songs", "bible",
    // "announcements", "photos", "videos", "playlists", "timers".
    void openSection(const QString &key);
    ContentType selectedCategory() const { return m_selectedCategory; }

signals:
    void categorySelected(ContentType type);
    void addRequested();
    void playlistsRequested();
    void videoRequested();
    void timersRequested();
    void importExportRequested();
    void settingsRequested();
    void helpRequested();

private:
    struct CategoryRow {
        SidebarNavRow *row;
        ContentType type;
    };

    void selectCategory(ContentType type);
    void activateRow(SidebarNavRow *active);

    QList<CategoryRow> m_categoryRows;
    QList<SidebarNavRow *> m_allRows;
    SidebarNavRow *m_videoRow = nullptr;
    SidebarNavRow *m_playlistRow = nullptr;
    SidebarNavRow *m_timersRow = nullptr;
    SidebarNavRow *m_importExportRow = nullptr;
    SidebarNavRow *m_settingsRow = nullptr;
    SidebarNavRow *m_helpRow = nullptr;
    ContentType m_selectedCategory = ContentType::Song;
};
