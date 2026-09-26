#pragma once

#include "core/ContentItem.h"

#include <QFrame>
#include <QList>

class QLabel;
class QPushButton;
class QListWidget;
class QWidget;

// The right panel's "Быстрый список" (Quick List) card from design.pen: a
// temporary, in-memory queue of songs for the current service — not
// persisted anywhere, per its own helper text ("песни остаются в вашей
// базе"). Rows are drag-reorderable; clicking a row sends it live and
// highlights it; the highlighted row's action icon becomes "play" instead
// of the remove "x".
class QuickListCard : public QFrame {
    Q_OBJECT
public:
    explicit QuickListCard(QWidget *parent = nullptr);

    // Appends a song to the queue. Called by whoever owns this card once
    // they've resolved what "the current song" is (this card has no
    // knowledge of the rest of the app).
    void addItem(const ContentItem &item);

signals:
    void goLiveRequested(const ContentItem &item, int slideIndex);
    void addCurrentSongRequested();

private:
    struct Entry {
        int rowId;
        ContentItem item;
    };

    void buildUi();
    void rebuildList();
    void updateTitle();
    void setCollapsed(bool collapsed);
    void setCurrentRow(int rowId);

    QList<Entry> m_entries;
    int m_currentRowId = -1;
    int m_nextRowId = 1;
    bool m_collapsed = false;

    QLabel *m_titleLabel = nullptr;
    QPushButton *m_collapseButton = nullptr;
    QWidget *m_body = nullptr;
    QListWidget *m_list = nullptr;
};
