#pragma once
#include <QImage>

struct BwParams {
    int reds     = 0;
    int yellows  = 0;
    int greens   = 0;
    int cyans    = 0;
    int blues    = 0;
    int magentas = 0;
};

namespace BwConverter {
    QImage   convert(const QImage &src, const BwParams &params);
    BwParams autoParams(const QImage &src);
}
