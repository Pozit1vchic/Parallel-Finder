#pragma once

#include <QImage>
#include <QImageWriter>
#include <QString>

namespace pfservices {

// PNG stays lossless. Lower compression spends less CPU on disposable
// comparison thumbnails; it does not change the pixels or image dimensions.
inline bool saveLosslessPreview(const QImage& image, const QString& path)
{
    QImageWriter writer(path, "png");
    // Qt PNG maps 0..100 to zlib 0..9; 12 selects level 1, not level 0.
    writer.setCompression(12);
    return writer.write(image);
}

} // namespace pfservices
