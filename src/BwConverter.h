#pragma once
#include <QImage>

// A named black-&-white conversion technique ("look"). Each look fixes a set of
// channel weights, a working colour space, and a tonal curve — see BwConverter.cpp.
enum class BwLook {
    Neutral,       // Perceptual luminance (BT.709, linear light). Accurate, flat.
    Photoshop,     // Photoshop "Black & White" default channel mix.
    IPhone,        // Punchy phone-camera look: deep blacks, protected highlights.
    Monochrom,     // Panchromatic sensor (à la Leica Monochrom): true, wide range.
    ClassicLuma,   // Rec.601 luma — the ubiquitous "digital grayscale".
    Film,          // Tri-X / Ilford film curve: lifted toe, rolled shoulder.
    HighContrast,  // Strong S-curve, dramatic blacks and whites.
};

struct BwParams {
    BwLook look = BwLook::Neutral;

    // Hue-band adjustments layered on top of the look (-100..100): nudge each
    // family of colours lighter (+) or darker (-) in the final grey.
    int reds     = 0;
    int yellows  = 0;
    int greens   = 0;
    int cyans    = 0;
    int blues    = 0;
    int magentas = 0;

    // Contrast (-100..100): strength of the look's tonal S-curve. Each look
    // preloads its own default (see lookPreset); the slider deviates from there.
    int contrast = 0;
};

namespace BwConverter {
    // Convert a colour image to Format_Grayscale16 using the given look + sliders.
    QImage      convert(const QImage &src, const BwParams &params);

    // The default slider state for a look (look selected, hue bands zeroed,
    // contrast set to the look's built-in default). Used when a look is picked.
    BwParams    lookPreset(BwLook look);

    // Human-readable name for a look (UI labels / tooltips).
    const char *lookName(BwLook look);
}
