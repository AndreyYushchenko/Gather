#pragma once

#include "core/BibleRepository.h"
#include "core/ContentItem.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QHBoxLayout;
class QScrollArea;
class ReferenceField;

// "Bible Content" screen: a live reference picker over the imported
// translation (see scripts/import_bible.py) instead of a saved-items list —
// the operator browses the whole Bible and "В избранное" promotes a
// reference into the regular library (no dedicated saved-references list on
// this screen — design.pen dropped that in favor of just the library itself).
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
    void setSlidesCollapsed(bool collapsed);
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
    void runSearch(const QString &text);
    // Недавние места / история поиска (Настройки → Библия).
    void rememberPlace();
    void rememberSearch(const QString &text);
    void showRecentMenu();
    QString bookName(int bookNum) const;
    ContentItem currentItem() const;

    BibleRepository m_repo;
    QString m_translation;
    QString m_altTranslation; // "Показывать альтернативный перевод"
    QList<BibleBook> m_books;

    int m_currentBook = 0;
    int m_currentChapter = 1;
    int m_currentFrom = 1;
    int m_currentTo = 1;
    QStringList m_slides;
    int m_selectedSlide = 0;

    // Tabs — "По тексту" (design.pen node i87VO3) is the reference/verse
    // browser below, "По поиску" (n4lRI) is the full-text search page.
    QPushButton *m_tabReference = nullptr;
    QPushButton *m_tabSearch = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    // Reference picker. m_translationField/m_bookField own the two combos
    // that can carry long text (translation name, book name) — reload()/
    // populateBooks() feed them full, unelided strings via setItems() so
    // they can re-elide live as the field resizes; m_translationBox/
    // m_bookBox are just their .combo() for the rest of the class to
    // connect to / read from, same as m_chapterBox/m_fromBox/m_toBox.
    ReferenceField *m_translationField = nullptr;
    ReferenceField *m_bookField = nullptr;
    QComboBox *m_translationBox = nullptr;
    QComboBox *m_bookBox = nullptr;
    QComboBox *m_chapterBox = nullptr;
    QComboBox *m_fromBox = nullptr;
    QComboBox *m_toBox = nullptr;

    // Verse detail column
    QLabel *m_refTitle = nullptr;
    QLabel *m_translationCaption = nullptr;
    QPushButton *m_favoriteButton = nullptr;
    QVBoxLayout *m_versesLayout = nullptr;
    QLabel *m_slidesHeading = nullptr;
    // "Скрыть слайды" — the same chevron as the song slide strip.
    QScrollArea *m_slidesScroll = nullptr;
    QPushButton *m_slidesCollapseButton = nullptr;
    class QVariantAnimation *m_slidesCollapseAnimation = nullptr;
    bool m_slidesCollapsed = false;
    QHBoxLayout *m_slidesRow = nullptr;

    // Search tab
    QLineEdit *m_searchBox = nullptr;
    QPushButton *m_recentButton = nullptr;
    class QStringListModel *m_searchHistory = nullptr;
    QVBoxLayout *m_searchResults = nullptr;
};
