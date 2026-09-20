#pragma once

#include "core/ContentItem.h"

#include <QMap>
#include <QWidget>

class QLabel;

// "Импорт / Экспорт" screen: import dropzone + recent-imports card on the
// left, export checklist (real per-category counts) + export button on the
// right. Both columns funnel into the same backup export/import flow that
// already exists in MainWindow (there's only one real transfer format today
// — a timestamped backup folder — so the design's per-format import dropzone
// and ZIP export are both mapped onto it rather than duplicated).
class ImportExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ImportExportPanel(QWidget *parent = nullptr);

    void setCounts(const QMap<ContentType, int> &counts, int playlistCount);

signals:
    void actionRequested();

private:
    void buildUi();

    QLabel *m_songsCount = nullptr;
    QLabel *m_bibleCount = nullptr;
    QLabel *m_announcementsCount = nullptr;
    QLabel *m_photosCount = nullptr;
    QLabel *m_playlistsCount = nullptr;
};
