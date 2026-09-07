#include "svnrevrange.h"

#include <algorithm>

QString formatRevisionRanges(QList<long long> revs)
{
    std::sort(revs.begin(), revs.end());
    revs.erase(std::unique(revs.begin(), revs.end()), revs.end());

    QStringList parts;
    int i = 0;
    while (i < revs.size()) {
        int j = i;
        while (j + 1 < revs.size() && revs[j + 1] == revs[j] + 1)
            ++j;
        if (j > i)
            parts << QStringLiteral("%1-%2").arg(revs[i]).arg(revs[j]);
        else
            parts << QString::number(revs[i]);
        i = j + 1;
    }
    return parts.join(QLatin1Char(','));
}
