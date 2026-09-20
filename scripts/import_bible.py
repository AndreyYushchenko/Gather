#!/usr/bin/env python3
"""One-off importer: loads a .spb Bible text dump into the Gather SQLite DB.

Usage: python import_bible.py <path-to-spb> <path-to-gather.db>
"""
import sqlite3
import sys


def parse_spb(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    title = ""
    i = 0
    while i < len(lines) and lines[i].startswith("##"):
        if lines[i].startswith("##Title:"):
            title = lines[i].split("\t", 1)[1].strip()
        i += 1

    books = {}  # book_num -> (name, chapter_count)
    while i < len(lines) and not lines[i].startswith("-----"):
        line = lines[i].strip()
        if line:
            num_str, name, chap_str = line.split("\t")
            books[int(num_str)] = (name, int(chap_str))
        i += 1
    i += 1  # skip the "-----" separator

    # A handful of verses are split across two source lines that reuse the
    # same verse number (poetry line breaks in the original edition) rather
    # than genuinely conflicting content, so merge same-key lines in order.
    merged = {}
    order = []
    for line in lines[i:]:
        if not line.strip():
            continue
        _ref, book_str, chap_str, verse_str, text = line.split("\t", 4)
        key = (int(book_str), int(chap_str), int(verse_str))
        if key in merged:
            merged[key] += " " + text
        else:
            merged[key] = text
            order.append(key)

    verses = [(b, c, v, merged[(b, c, v)]) for b, c, v in order]
    return title, books, verses


def main():
    if len(sys.argv) != 3:
        print("Usage: import_bible.py <spb-file> <gather.db>")
        sys.exit(1)

    spb_path, db_path = sys.argv[1], sys.argv[2]
    title, books, verses = parse_spb(spb_path)
    print(f"Translation: {title}")
    print(f"Books: {len(books)}, verses: {len(verses)}")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    cur.execute("""
        CREATE TABLE IF NOT EXISTS bible_books (
            translation TEXT NOT NULL,
            book_num INTEGER NOT NULL,
            book_name TEXT NOT NULL,
            chapter_count INTEGER NOT NULL,
            PRIMARY KEY (translation, book_num)
        )
    """)
    cur.execute("""
        CREATE TABLE IF NOT EXISTS bible_verses (
            translation TEXT NOT NULL,
            book_num INTEGER NOT NULL,
            chapter INTEGER NOT NULL,
            verse INTEGER NOT NULL,
            text TEXT NOT NULL,
            PRIMARY KEY (translation, book_num, chapter, verse)
        )
    """)
    cur.execute("CREATE INDEX IF NOT EXISTS idx_bible_verses_lookup ON bible_verses(translation, book_num, chapter)")

    cur.execute("DELETE FROM bible_books WHERE translation = ?", (title,))
    cur.execute("DELETE FROM bible_verses WHERE translation = ?", (title,))

    cur.executemany(
        "INSERT INTO bible_books (translation, book_num, book_name, chapter_count) VALUES (?, ?, ?, ?)",
        [(title, num, name, cc) for num, (name, cc) in books.items()],
    )
    cur.executemany(
        "INSERT INTO bible_verses (translation, book_num, chapter, verse, text) VALUES (?, ?, ?, ?, ?)",
        [(title, b, c, v, t) for b, c, v, t in verses],
    )

    conn.commit()
    print(f"Imported {cur.execute('SELECT COUNT(*) FROM bible_verses WHERE translation = ?', (title,)).fetchone()[0]} verses.")
    conn.close()


if __name__ == "__main__":
    main()
