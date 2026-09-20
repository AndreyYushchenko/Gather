#pragma once

#include "core/ContentItem.h"
#include "core/ContentRepository.h"

#include <QWidget>
#include <optional>

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QTimer;

// Middle "library" column: search, sort, and the row list for whichever
// category the sidebar currently has selected.
class LibraryListPanel : public QWidget {
    Q_OBJECT
public:
    explicit LibraryListPanel(QWidget *parent = nullptr);

    void setCategory(ContentType type);
    void setItems(const QList<ContentItem> &items);
    std::optional<int> selectedItemId() const;
    void selectItemById(int id);
    bool favoritesOnly() const { return m_favoritesOnly; }
    QString searchText() const;
    SortOrder sortOrder() const;

signals:
    void filtersChanged();
    void itemSelected(std::optional<int> id);
    void favoriteToggled(int id);

private:
    void updateSortLabel();

    QList<ContentItem> m_items;
    bool m_favoritesOnly = false;
    ContentType m_category = ContentType::Song;
    SortOrder m_sortOrder = SortOrder::Number;

    QLineEdit *m_search = nullptr;
    QPushButton *m_filterButton = nullptr;
    QPushButton *m_sortButton = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_searchDebounce = nullptr;
};
