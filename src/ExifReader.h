#pragma once
#include <QMap>
#include <QString>

namespace ExifReader {
    // Named fields populated by read(). All values are human-readable strings.
    //
    // File info (always present when path is readable):
    //   "FileName"        — e.g. "Canon_40D.jpg"
    //   "FileSize"        — e.g. "8.3 MB"
    //   "Dimensions"      — e.g. "3888 × 2592"
    //
    // EXIF fields (present only when EXIF data is found):
    //   "Make"            — camera manufacturer, e.g. "Canon"
    //   "Model"           — camera model, e.g. "Canon EOS 40D"
    //   "Camera"          — Make + Model combined
    //   "Software"        — editing/firmware software
    //   "DateTime"        — shoot date/time, e.g. "2008-05-30 15:56:01"
    //   "Exposure"        — composed: "1/160 s   f/7.1   ISO 100"
    //   "ExposureTime"    — shutter speed alone, e.g. "1/160 s"
    //   "FNumber"         — aperture alone, e.g. "f/7.1"
    //   "ISO"             — ISO speed alone, e.g. "ISO 100"
    //   "FocalLength"     — focal length with optional 35mm equiv, e.g. "135 mm   (35mm: 85 mm)"
    //   "FocalLength35mm" — 35mm equivalent alone, e.g. "85 mm"
    //   "ExposureBias"    — non-zero bias only, e.g. "+0.3 EV"
    //   "ExposureProgram" — e.g. "Manual", "Aperture priority"
    //   "MeteringMode"    — e.g. "Pattern", "Spot"
    //   "Flash"           — present only when flash fired: "Fired"
    //   "GPS"             — e.g. "43°28'2.8\"N  11°53'6.5\"E"
    //   "GPSLatitude"     — raw decimal latitude, e.g. "43.4675000"
    //   "GPSLongitude"    — raw decimal longitude, e.g. "11.8851389"
    //   "LensMake"        — lens manufacturer
    //   "LensModel"       — lens model string
    //   "Copyright"       — copyright string
    using ExifData = QMap<QString, QString>;

    ExifData read(const QString &filePath);
}
