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

// Left column of the Video screen: upload a local file or paste a YouTube
// link (downloaded via yt-dlp — same mechanism as ItemEditDialog's
// background-video import), then search/sort/browse the video library.
class VideoListPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoListPanel(QWidget *parent = nullptr);

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
    // The panel only picks/downloads the file; MainWindow persists it via
    // ContentRepository and calls setItems() again, same division of labor
    // as ItemEditDialog::accept() elsewhere in the app.
    void videoAdded(ContentItem item);

private:
    void updateSortLabel();
    void browseForVideo();
    void importFromYoutube();

    QList<ContentItem> m_items;
    bool m_favoritesOnly = false;
    SortOrder m_sortOrder = SortOrder::DateAddedDesc;

    QPushButton *m_uploadButton = nullptr;
    QLineEdit *m_youtubeUrl = nullptr;
    QPushButton *m_youtubeButton = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_filterButton = nullptr;
    QPushButton *m_sortButton = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_searchDebounce = nullptr;
};
