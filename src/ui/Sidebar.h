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
    ContentType selectedCategory() const { return m_selectedCategory; }

signals:
    void categorySelected(ContentType type);
    void addRequested();
    void playlistsRequested();
    void videoRequested();
    void importExportRequested();
    void settingsRequested();
    void helpRequested();

private:
    struct CategoryRow {
        SidebarNavRow *row;
        ContentType type;
    };

    void selectCategory(ContentType type);

    QList<CategoryRow> m_categoryRows;
    ContentType m_selectedCategory = ContentType::Song;
};
