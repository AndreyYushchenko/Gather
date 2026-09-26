#pragma once

#include "core/ContentItem.h"

#include <QHash>
#include <QList>
#include <QWidget>

class ContentRepository;
class ClickFrame;
class DropdownField;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;

// "Фото" (design.pen node EqavR): search, "Добавить фото", grid/list view
// toggle and date/name sort over the photo library. Each photo shows its
// file name and a "⋯" menu; double-click (or Enter) starts a slideshow on
// the projector from that photo, stepped with Назад/Вперёд.
class PhotosPanel : public QWidget {
    Q_OBJECT
public:
    explicit PhotosPanel(ContentRepository *repository, QWidget *parent = nullptr);

    // Re-reads the photos from the database.
    void reload();
    // "Добавить фото": file picker, then import.
    void importPhotos();
    // The selected photos to the projector.
    void showSelected();

signals:
    void goLiveRequested(const QList<ContentItem> &photos, int startIndex);
    // Photos were added or removed (sidebar count).
    void libraryChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *buildToolbar();
    void setListView(bool list);
    void rebuildItems();
    void refreshSelection();

    QList<ContentItem> visiblePhotos() const;
    const ContentItem *photo(int id) const;
    QList<ContentItem> selectedItems() const;

    void clickItem(int id, Qt::KeyboardModifiers modifiers);
    void showItemMenu(int id, const QPoint &globalPos);
    void startShow(int fromId);
    void showPreview(int id);
    void renamePhoto(int id);
    void deleteSelection();
    void importFiles(const QStringList &paths);

    ContentRepository *m_repository;
    QList<ContentItem> m_photos;

    QString m_search;
    int m_sort = 0; // 0 newest, 1 oldest, 2 by name
    bool m_listView = false;

    QList<int> m_shownIds; // display order
    QList<int> m_selection;
    int m_anchorId = -1;

    QLineEdit *m_searchEdit = nullptr;
    ClickFrame *m_gridButton = nullptr;
    ClickFrame *m_listButton = nullptr;
    DropdownField *m_sortField = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_items = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    QHash<int, QWidget *> m_itemWidgets;
};
