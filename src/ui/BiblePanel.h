#pragma once

#include "core/BibleRepository.h"
#include "core/ContentItem.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QStackedWidget;
class QVBoxLayout;
class QHBoxLayout;
class QScrollArea;

// "Bible Content" screen: a live reference picker over the imported
// translation (see scripts/import_bible.py) instead of a saved-items list —
// the operator browses the whole Bible and only "Добавить в избранное"
// promotes a reference into the regular library.
class BiblePanel : public QWidget {
    Q_OBJECT
public:
    explicit BiblePanel(QWidget *parent = nullptr);

    // Re-reads the translation/book list; call after import or on first show.
    void reload();

    // Sends the current reference/slide live, exactly like clicking
    // "На экран" — used by the app-wide F5 shortcut.
    void triggerGoLive();

signals:
    void previewRequested(const ContentItem &item, int slideIndex);
    void goLiveRequested(const ContentItem &item, int slideIndex);
    void saveToLibraryRequested(ContentItem item);

private:
    void buildUi();
    void populateBooks();
    void onBookChanged();
    void onChapterChanged();
    void onRangeChanged();
    void selectReference(int bookNum, int chapter, int fromVerse, int toVerse);
    void refreshVerseView();
    void rebuildSlides();
    void applySlideTextChange(const QStringList &newSlides);
    void selectSlide(int index);
    void pushRecent(int bookNum, int chapter, int fromVerse, int toVerse);
    void rebuildRecentList();
    void runSearch(const QString &text);
    QString bookName(int bookNum) const;
    ContentItem currentItem() const;

    BibleRepository m_repo;
    QString m_translation;
    QList<BibleBook> m_books;

    int m_currentBook = 0;
    int m_currentChapter = 1;
    int m_currentFrom = 1;
    int m_currentTo = 1;
    QStringList m_slides;
    int m_selectedSlide = 0;

    struct RecentEntry { int book; int chapter; int from; int to; };
    QList<RecentEntry> m_recent;

    // Header
    QPushButton *m_favoriteButton = nullptr;

    // Tabs
    QPushButton *m_tabByRef = nullptr;
    QPushButton *m_tabByText = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    // Reference picker
    QComboBox *m_translationBox = nullptr;
    QComboBox *m_bookBox = nullptr;
    QComboBox *m_chapterBox = nullptr;
    QComboBox *m_fromBox = nullptr;
    QComboBox *m_toBox = nullptr;

    // Places column
    QVBoxLayout *m_recentList = nullptr;

    // Verse detail column
    QLabel *m_refTitle = nullptr;
    QLabel *m_translationCaption = nullptr;
    QVBoxLayout *m_versesLayout = nullptr;
    QLabel *m_slidesHeading = nullptr;
    QCheckBox *m_splitCheckBox = nullptr;
    QHBoxLayout *m_slidesRow = nullptr;

    // Search tab
    QLineEdit *m_searchBox = nullptr;
    QVBoxLayout *m_searchResults = nullptr;
};
