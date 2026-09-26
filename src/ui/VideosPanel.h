#pragma once

#include "core/ContentItem.h"

#include <QHash>
#include <QList>
#include <QWidget>

class ContentRepository;
class ClickFrame;
class DropdownField;
class QFrame;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;

// "Видео" (design.pen "Sermon App - Video"): search, "Добавить видео ⌄"
// (file / YouTube link), grid/list toggle and sort over the video library;
// tiles show a poster frame, duration, format and a "⋯" menu. The bottom
// bar acts on the selection: delete, preview, "На экран ⌄" (normal or as a
// looping muted background).
class VideosPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideosPanel(ContentRepository *repository, QWidget *parent = nullptr);

    void reload();
    // F5 / "На экран" for the current selection.
    void goLiveSelected(bool loop = false);

signals:
    void goLiveRequested(const ContentItem &item, bool loop);
    void libraryChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *buildToolbar();
    QWidget *buildSelectionBar();
    void setListView(bool list);
    void rebuildItems();
    void refreshSelection();

    QList<ContentItem> visibleVideos() const;
    const ContentItem *video(int id) const;
    QList<ContentItem> selectedItems() const;

    void clickItem(int id, Qt::KeyboardModifiers modifiers);
    void showItemMenu(int id, const QPoint &globalPos);
    void showAddMenu(QWidget *anchor);
    void showOnScreenMenu(QWidget *anchor);
    void preview(const ContentItem &item);
    void renameVideo(int id);
    void deleteSelection();
    void importFiles(const QStringList &paths);
    void importFromLink();
    void addItem(ContentItem item);

    ContentRepository *m_repository;
    QList<ContentItem> m_videos;

    QString m_search;
    int m_sort = 0; // 0 newest, 1 oldest, 2 by name
    bool m_listView = false;
    bool m_downloading = false;

    QList<int> m_shownIds;
    QList<int> m_selection;
    int m_anchorId = -1;

    QLineEdit *m_searchEdit = nullptr;
    ClickFrame *m_addButton = nullptr;
    ClickFrame *m_gridButton = nullptr;
    ClickFrame *m_listButton = nullptr;
    DropdownField *m_sortField = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_items = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    QHash<int, QWidget *> m_itemWidgets;

    QFrame *m_selectionBar = nullptr;
    QLabel *m_selectionLabel = nullptr;
};
