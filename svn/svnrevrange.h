#pragma once

#include <QList>
#include <QString>

/**
 * Formats revision numbers into "5-10,14": consecutive runs are collapsed into
 * a-b, compatible with 'svn merge -c' and the ranges field of the merge dialog.
 * Sorts and deduplicates the input.
 */
QString formatRevisionRanges(QList<long long> revs);
