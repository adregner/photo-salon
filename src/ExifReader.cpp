#include "ExifReader.h"
#include "exif.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <cmath>

namespace ExifReader {

namespace {

static QString fmtShutter(double seconds) {
    if (seconds <= 0) return {};
    if (seconds >= 1.0)
        return QString("%1 s").arg(seconds, 0, 'f', seconds < 10 ? 1 : 0);
    return QString("1/%1 s").arg(qRound(1.0 / seconds));
}

static QString fmtFNumber(double f) {
    if (f <= 0) return {};
    double r = std::round(f * 10) / 10;
    return (r == std::round(r))
        ? QString("f/%1").arg((int)r)
        : QString("f/%1").arg(r, 0, 'f', 1);
}

static QString fmtFocalLength(double mm) {
    if (mm <= 0) return {};
    return (mm == std::round(mm))
        ? QString("%1 mm").arg((int)std::round(mm))
        : QString("%1 mm").arg(mm, 0, 'f', 1);
}

static QString fmtDate(const std::string &s) {
    if (s.empty()) return {};
    QString dt = QString::fromStdString(s);
    // EXIF: "2024:03:15 14:32:01" → "2024-03-15 14:32:01"
    if (dt.length() >= 10 && dt[4] == ':' && dt[7] == ':')
        dt = dt.left(4) + '-' + dt.mid(5, 2) + '-' + dt.mid(8, 2) + dt.mid(10);
    return dt;
}

static QString fmtGps(double lat, double lon) {
    if (std::abs(lat) < 1e-6 && std::abs(lon) < 1e-6) return {};
    auto dms = [](double deg, char pos, char neg) -> QString {
        double a = std::abs(deg);
        int d    = (int)a;
        double mf = (a - d) * 60.0;
        int m    = (int)mf;
        double s = (mf - m) * 60.0;
        return QString("%1°%2'%3\"%4")
            .arg(d).arg(m).arg(s, 0, 'f', 1).arg(deg >= 0 ? pos : neg);
    };
    return dms(lat, 'N', 'S') + "  " + dms(lon, 'E', 'W');
}

static QString fmtMeteringMode(unsigned short v) {
    switch (v) {
    case 1: return "Average";
    case 2: return "Center-weighted";
    case 3: return "Spot";
    case 4: return "Multi-spot";
    case 5: return "Multi-segment";
    case 6: return "Partial";
    default: return {};
    }
}

static QString fmtExposureProgram(unsigned short v) {
    switch (v) {
    case 1: return "Manual";
    case 2: return "Auto";
    case 3: return "Aperture priority";
    case 4: return "Shutter priority";
    case 5: return "Creative";
    case 6: return "Action";
    case 7: return "Portrait";
    case 8: return "Landscape";
    default: return {};
    }
}

} // anonymous namespace

ExifData read(const QString &filePath) {
    ExifData data;

    QFileInfo fi(filePath);
    data["FileName"] = fi.fileName();

    qint64 bytes = fi.size();
    if (bytes > 1024*1024)
        data["FileSize"] = QString("%1 MB").arg((double)bytes / (1024*1024), 0, 'f', 1);
    else if (bytes > 1024)
        data["FileSize"] = QString("%1 KB").arg((double)bytes / 1024, 0, 'f', 0);
    else
        data["FileSize"] = QString("%1 B").arg(bytes);

    {
        QImageReader reader(filePath);
        QSize sz = reader.size();
        if (sz.isValid())
            data["Dimensions"] = QString("%1 × %2").arg(sz.width()).arg(sz.height());
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return data;
    QByteArray raw = file.readAll();
    file.close();

    easyexif::EXIFInfo exif;
    if (exif.parseFrom(reinterpret_cast<const unsigned char *>(raw.constData()),
                       static_cast<unsigned>(raw.size())) != PARSE_EXIF_SUCCESS)
        return data;

    auto qstr = [](const std::string &s) {
        return QString::fromStdString(s).remove(QChar('\0')).trimmed();
    };

    if (!exif.Make.empty())  data["Make"]  = qstr(exif.Make);
    if (!exif.Model.empty()) data["Model"] = qstr(exif.Model);

    {
        QString camera = QStringList{qstr(exif.Make), qstr(exif.Model)}
                             .join(' ').simplified();
        if (!camera.isEmpty()) data["Camera"] = camera;
    }

    if (!exif.Software.empty()) data["Software"] = qstr(exif.Software);

    {
        const std::string &dt = !exif.DateTimeOriginal.empty()
                                    ? exif.DateTimeOriginal : exif.DateTime;
        QString fmt = fmtDate(dt);
        if (!fmt.isEmpty()) data["DateTime"] = fmt;
    }

    if (exif.ExposureTime > 0) data["ExposureTime"] = fmtShutter(exif.ExposureTime);
    if (exif.FNumber > 0)      data["FNumber"]      = fmtFNumber(exif.FNumber);
    if (exif.ISOSpeedRatings > 0)
        data["ISO"] = QString("ISO %1").arg(exif.ISOSpeedRatings);

    {
        QStringList parts;
        for (const QString &k : {"ExposureTime", "FNumber", "ISO"})
            if (data.contains(k)) parts << data[k];
        if (!parts.isEmpty()) data["Exposure"] = parts.join("   ");
    }

    if (exif.FocalLength > 0) {
        QString fl = fmtFocalLength(exif.FocalLength);
        if (exif.FocalLengthIn35mm > 0) {
            data["FocalLength35mm"] = QString("%1 mm").arg(exif.FocalLengthIn35mm);
            fl += QString("   (35mm: %1 mm)").arg(exif.FocalLengthIn35mm);
        }
        data["FocalLength"] = fl;
    }

    if (std::abs(exif.ExposureBiasValue) > 0.01) {
        double ev = exif.ExposureBiasValue;
        data["ExposureBias"] = QString("%1%2 EV")
            .arg(ev > 0 ? "+" : "")
            .arg(ev, 0, 'f', 1);
    }

    {
        QString ep = fmtExposureProgram(exif.ExposureProgram);
        if (!ep.isEmpty()) data["ExposureProgram"] = ep;
    }

    {
        QString mm = fmtMeteringMode(exif.MeteringMode);
        if (!mm.isEmpty()) data["MeteringMode"] = mm;
    }

    if (exif.Flash) data["Flash"] = "Fired";

    {
        double lat = exif.GeoLocation.Latitude;
        double lon = exif.GeoLocation.Longitude;
        QString gps = fmtGps(lat, lon);
        if (!gps.isEmpty()) {
            data["GPS"]          = gps;
            data["GPSLatitude"]  = QString::number(lat, 'f', 7);
            data["GPSLongitude"] = QString::number(lon, 'f', 7);
        }
    }

    if (!exif.LensInfo.Make.empty())  data["LensMake"]  = qstr(exif.LensInfo.Make);
    if (!exif.LensInfo.Model.empty()) data["LensModel"] = qstr(exif.LensInfo.Model);

    if (!exif.Copyright.empty()) {
        QString c = qstr(exif.Copyright);
        if (!c.isEmpty()) data["Copyright"] = c;
    }

    return data;
}

} // namespace ExifReader
