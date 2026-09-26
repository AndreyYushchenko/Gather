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
class AnnouncementEditor;

// "Объявления" (design.pen node cB8xz): search, "Добавить объявление",
// grid/list toggle and sort over the announcements, plus the
// "Редактирование объявления" card on the right for the selected one.
// Double-click (or Enter / F5) puts an announcement on the projector.
class AnnouncementsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnnouncementsPanel(ContentRepository *repository, QWidget *parent = nullptr);

    // Re-reads the announcements from the database.
    void reload();
    // "Добавить объявление": opens an empty draft in the editor.
    void createNew();
    // F5: the selected announcement on screen.
    void goLiveSelected();
    int selectedId() const { return m_selectedId; }
    void selectById(int id) { select(id); }
    // Настройки saved: autosave on/off and its interval.
    void applySettings();

signals:
    void goLiveRequested(const ContentItem &item);
    void previewRequested(const ContentItem &item);
    // Saved/added/removed (sidebar count, live refresh).
    void announcementSaved(const ContentItem &item);
    void libraryChanged();

private:
    QWidget *buildToolbar();
    void setListView(bool list);
    void rebuildItems();
    void refreshSelection();
    QList<ContentItem> visibleItems() const;
    const ContentItem *announcement(int id) const;

    // Returns false when the user cancelled leaving unsaved edits.
    bool confirmLeaveEditor();
    void select(int id);
    void showItemMenu(int id, const QPoint &globalPos);
    void duplicate(int id);
    void remove(int id);
    void save(ContentItem item);

    ContentRepository *m_repository;
    QList<ContentItem> m_items;

    QString m_search;
    int m_sort = 0; // 0 newest, 1 oldest, 2 by title
    bool m_listView = false;
    int m_selectedId = -1;

    QLineEdit *m_searchEdit = nullptr;
    ClickFrame *m_gridButton = nullptr;
    ClickFrame *m_listButton = nullptr;
    DropdownField *m_sortField = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_itemsWidget = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    QHash<int, QWidget *> m_itemWidgets;
    AnnouncementEditor *m_editor = nullptr;
    class QTimer *m_autosave = nullptr;
};
