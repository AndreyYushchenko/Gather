#pragma once

#include "core/ContentItem.h"

#include <QWidget>
#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QSlider;
class QTimer;
class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;

// Center panel of the Video screen: header (title/star/edit/delete), an
// output-mode toggle (looping background vs. a direct one-shot show), the
// three tabs from the design (preview / info / notes), and an embedded
// player for local files. A remote (YouTube) source can't be decoded
// in-app, so its preview instead points the operator at the OBS/browser
// output where it actually plays (see SlideRenderWidget/DisplayServer).
class VideoDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoDetailPanel(QWidget *parent = nullptr);

    void showItem(const std::optional<ContentItem> &item);
    void triggerGoLive();

signals:
    void deleteRequested(int id);
    void favoriteToggleRequested(int id);
    void titleChangeRequested(int id, const QString &title);
    void notesChanged(int id, const QString &notes);
    void replaceRequested(int id, const QString &newLocalPath);
    void previewRequested(const ContentItem &item, bool loop);
    void goLiveRequested(const ContentItem &item, bool loop);

private:
    void buildUi();
    void applyItemToPlayer();
    void updateMetaLabel();
    void togglePlayback();

    std::optional<ContentItem> m_item;
    bool m_loopMode = false; // false = "Прямой показ на экран", true = "Как фон (в цикле)"

    // Header
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_starButton = nullptr;
    QLabel *m_metaLabel = nullptr;

    // Output mode
    QPushButton *m_modeBackground = nullptr;
    QPushButton *m_modeFullscreen = nullptr;

    // Tabs
    QPushButton *m_tabPreview = nullptr;
    QPushButton *m_tabInfo = nullptr;
    QPushButton *m_tabNotes = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    // Tab 0: preview
    QWidget *m_previewBox = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QMediaPlayer *m_mediaPlayer = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QLabel *m_remoteNotice = nullptr;
    QPushButton *m_playButton = nullptr;
    QLabel *m_timeCurrentLabel = nullptr;
    QLabel *m_timeTotalLabel = nullptr;
    QSlider *m_timelineSlider = nullptr;
    bool m_scrubbing = false;

    // Tab 1: info
    QLabel *m_infoText = nullptr;

    // Tab 2: notes
    QTextEdit *m_notesEdit = nullptr;
    QTimer *m_notesSaveTimer = nullptr;

    QLabel *m_emptyState = nullptr;
    QWidget *m_content = nullptr;
};
