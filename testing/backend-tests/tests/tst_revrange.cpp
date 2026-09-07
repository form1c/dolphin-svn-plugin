#include "svnrevrange.h"

#include <QTest>

// GUI-free unit test for formatRevisionRanges (F-TEST1) — no QtWidgets
// dependency in the test target.
class TstRevRange : public QObject
{
    Q_OBJECT

private slots:
    void emptyInput();
    void singleRevision();
    void mixedRangesAndSingles();
    void unsortedInput();
    void duplicateRevisions();
};

void TstRevRange::emptyInput()
{
    QCOMPARE(formatRevisionRanges({}), QString());
}

void TstRevRange::singleRevision()
{
    QCOMPARE(formatRevisionRanges({5}), QStringLiteral("5"));
}

void TstRevRange::mixedRangesAndSingles()
{
    QCOMPARE(formatRevisionRanges({5, 6, 7, 10}), QStringLiteral("5-7,10"));
}

void TstRevRange::unsortedInput()
{
    QCOMPARE(formatRevisionRanges({10, 5, 7, 6}), QStringLiteral("5-7,10"));
}

void TstRevRange::duplicateRevisions()
{
    QCOMPARE(formatRevisionRanges({5, 5, 6, 6, 7, 10, 10}), QStringLiteral("5-7,10"));
}

QTEST_GUILESS_MAIN(TstRevRange)
#include "tst_revrange.moc"
