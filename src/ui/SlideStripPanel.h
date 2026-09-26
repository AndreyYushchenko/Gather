#pragma once

#include "core/ContentItem.h"

#include <QWidget>
#include <optional>

class QHBoxLayout;
class QPushButton;
class QWidget;
class QScrollArea;
class QVariantAnimation;

// design.pen's "Slides Section": a full-width strip pinned below the
// list+detail row (design's "Top Row"), showing the selected item's slide
// thumbnails and the add/copy/delete/preview/go-live actions. Previously
// this lived inside DetailPanel's own scrollable card; the design has it as
// a separate, always-visible sibling instead, so it's its own widget here
// too, fed the same item DetailPanel shows.
class SlideStripPanel : public QWidget {
    Q_OBJECT
public:
    explicit SlideStripPanel(QWidget *parent = nullptr);

    void showItem(const std::optional<ContentItem> &item);

    // Sends the currently selected slide live, exactly like clicking
    // "На экран" — used by the app-wide F5 shortcut.
    void triggerGoLive();

signals:
    void previewRequested(const ContentItem &item, int slideIndex);
    void goLiveRequested(const ContentItem &item, int slideIndex);
    void itemTextUpdated(const ContentItem &item);

private:
    void buildUi();
    void rebuildSlides();
    void selectSlide(int index);
    void applySlideTextChange(const QStringList &newSlides);
    QString joinSlides(const QStringList &slides) const;
    void setThumbnailsCollapsed(bool collapsed);
    void pageSlides(int direction);
    void updatePageButtons();

    std::optional<ContentItem> m_item;
    QStringList m_slides;
    int m_selectedSlide = 0;
    bool m_thumbnailsCollapsed = false;

    QWidget *m_body = nullptr;
    QHBoxLayout *m_slidesRow = nullptr;
    QScrollArea *m_slidesScroll = nullptr;
    QWidget *m_slidesRowContainer = nullptr;
    QPushButton *m_pageLeftButton = nullptr;
    QPushButton *m_pageRightButton = nullptr;
    QVariantAnimation *m_collapseAnimation = nullptr;
    QPushButton *m_collapseButton = nullptr;
    QPushButton *m_addSlideButton = nullptr;
    QPushButton *m_copySlideButton = nullptr;
    QPushButton *m_deleteSlideButton = nullptr;
    QPushButton *m_replacePhotoButton = nullptr;
};
