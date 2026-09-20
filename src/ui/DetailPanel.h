#pragma once

#include "core/ContentItem.h"
#include "core/ContentRepository.h"

#include <QWidget>
#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QHBoxLayout;
class QScrollArea;
class QTimer;

// Center panel: header (title/star/edit/delete), the three tabs from the
// design (text+slides / info / notes), and the slide thumbnails with their
// add/copy/delete/preview/go-live actions.
class DetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailPanel(QWidget *parent = nullptr);

    void showItem(const std::optional<ContentItem> &item);

    // Sends the currently shown item/slide live, exactly like clicking
    // "На экран" — used by the app-wide F5 shortcut.
    void triggerGoLive();

signals:
    void editRequested(int id);
    void deleteRequested(int id);
    void favoriteToggleRequested(int id);
    void notesChanged(int id, const QString &notes);
    void itemTextUpdated(const ContentItem &item);
    void previewRequested(const ContentItem &item, int slideIndex);
    void goLiveRequested(const ContentItem &item, int slideIndex);

private:
    void buildUi();
    void rebuildSlides();
    void selectSlide(int index);
    void applySlideTextChange(const QStringList &newSlides);
    QString joinSlides(const QStringList &slides) const;

    std::optional<ContentItem> m_item;
    QStringList m_slides;
    int m_selectedSlide = 0;

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
    QWidget *m_slidesSection = nullptr;
    QHBoxLayout *m_slidesRow = nullptr;
    QPushButton *m_addSlideButton = nullptr;
    QPushButton *m_copySlideButton = nullptr;
    QPushButton *m_deleteSlideButton = nullptr;
    QPushButton *m_replacePhotoButton = nullptr;

    // Tab 1: info
    QLabel *m_infoText = nullptr;

    // Tab 2: notes
    QTextEdit *m_notesEdit = nullptr;
    QTimer *m_notesSaveTimer = nullptr;

    QLabel *m_emptyState = nullptr;
    QWidget *m_content = nullptr;
};
