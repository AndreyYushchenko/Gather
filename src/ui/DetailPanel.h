#pragma once

#include "core/ContentItem.h"
#include "core/ContentRepository.h"

#include <QWidget>
#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QTimer;

// Center panel: header (title/star/edit/delete) and the three tabs from the
// design (text / info / notes). The slide thumbnails + their
// add/copy/delete/preview/go-live actions live below this, in
// SlideStripPanel — design.pen has them as a separate full-width strip
// under the list+detail row, not nested inside this card.
class DetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailPanel(QWidget *parent = nullptr);

    void showItem(const std::optional<ContentItem> &item);
    std::optional<ContentItem> currentItem() const { return m_item; }

signals:
    void editRequested(int id);
    void deleteRequested(int id);
    void favoriteToggleRequested(int id);
    void notesChanged(int id, const QString &notes);

private:
    void buildUi();

    std::optional<ContentItem> m_item;

    // Header
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_starButton = nullptr;
    QLabel *m_metaLabel = nullptr;

    // Tabs
    QPushButton *m_tabContent = nullptr;
    QPushButton *m_tabInfo = nullptr;
    QPushButton *m_tabNotes = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    // Tab 0: content
    QTextEdit *m_lyricsBox = nullptr;
    QLabel *m_photoBox = nullptr;

    // Tab 1: info
    QLabel *m_infoText = nullptr;

    // Tab 2: notes
    QTextEdit *m_notesEdit = nullptr;
    QTimer *m_notesSaveTimer = nullptr;

    QLabel *m_emptyState = nullptr;
    QWidget *m_content = nullptr;
};
