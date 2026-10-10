#include "AnalysisController.h"
#include "AppInfo.h"
#include <pfcore/VideoDecoder.hpp>
#include <pfservices/ResultCategoryStore.hpp>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <QDateTime>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace pfui {
namespace {
QString localPath(const QString& path) { return path.startsWith("file:") ? QUrl(path).toLocalFile() : path; }
QImage frameAt(pfcore::VideoDecoder& decoder, double seconds, std::stop_token stop = {}, double* decodedTime = nullptr) {
    pfcore::DecodedFrame frame;
    decoder.setRgbaMaxDimensions(960, 540);
    decoder.seek(seconds);
    bool found = false;
    const double halfFrame = 0.5 / std::max(1.0, decoder.info().frameRate);
    while (!stop.stop_requested() && decoder.readNext(frame, false)) {
        if (frame.timestampSeconds + halfFrame >= seconds) {
            found = decoder.convertCurrentFrameToRgba(frame); break;
        }
    }
    if (!found) throw std::runtime_error(stop.stop_requested() ? "Cancelled" : "Could not decode frame");
    if (decodedTime) *decodedTime = frame.timestampSeconds;
    return QImage(frame.rgba.data(), frame.width, frame.height, QImage::Format_RGBA8888)
        .copy();
}
QString timestamp(double value) {
    const auto ms = static_cast<qint64>(std::max(0.0, value) * 1000);
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}
}

void AnalysisController::pushReviewUndo(const QVariantList& ids, bool includeMatch) {
    std::vector<ReviewUndoRow> entry;
    std::set<int> used;
    for (const auto& value : ids) {
        const int id = value.toInt();
        if (id < 0 || id >= results_.size() || !used.insert(id).second) continue;
        ReviewUndoRow row{id, results_[id].toMap(), {}, {}};
        if (includeMatch && id < static_cast<int>(matches_.size())) row.match = matches_[id];
        const auto preview = reviewPreviewDirectories_.find(id);
        if (preview != reviewPreviewDirectories_.end()) row.preview = preview->second;
        entry.push_back(std::move(row));
    }
    if (entry.empty()) return;
    reviewUndo_.push_back(std::move(entry));
    // Bound both actions and retained rows; retain one complete large bulk action.
    std::size_t rows = 0; for (const auto& action : reviewUndo_) rows += action.size();
    while (reviewUndo_.size() > 1 && (reviewUndo_.size() > 16 || rows > 16384)) {
        rows -= reviewUndo_.front().size(); reviewUndo_.erase(reviewUndo_.begin());
    }
}

QVariantList AnalysisController::undoResultEdit() {
    if (busy_ || exportBusy_ || reviewBusy_ || reviewUndo_.empty()) return {};
    auto entry = std::move(reviewUndo_.back()); reviewUndo_.pop_back();
    QVariantList restored;
    for (auto& row : entry) {
        if (row.id < 0 || row.id >= results_.size()) continue;
        const auto current = results_[row.id].toMap();
        if (current.value("category") != row.record.value("category") || current.value("categoryColor") != row.record.value("categoryColor"))
            pfservices::ResultCategoryStore::save(row.record, row.record.value("category").toString(), row.record.value("categoryColor").toString());
        results_[row.id] = row.record;
        if (row.match && row.id < static_cast<int>(matches_.size())) matches_[row.id] = *row.match;
        if (row.preview) reviewPreviewDirectories_[row.id] = std::move(row.preview);
        else reviewPreviewDirectories_.erase(row.id);
        restored.push_back(row.id);
    }
    ++resultCategoryRevision_; emit resultCategoriesChanged();
    return restored;
}

void AnalysisController::setResultReview(const QVariantList& ids, const QString& field, bool value) {
    if (busy_ || exportBusy_ || reviewBusy_ || (field != "hidden" && field != "favorite" && field != "reviewed")) return;
    QVariantList changed;
    for (const auto& item : ids) {
        bool ok = false; const int id = item.toInt(&ok);
        if (!ok || id < 0 || id >= results_.size() || changed.contains(id)) continue;
        const auto row = results_[id].toMap();
        if (row.value("id").toInt() == id && row.value(field).toBool() != value) changed.push_back(id);
    }
    if (changed.isEmpty()) return;
    pushReviewUndo(changed);
    for (const auto& item : changed) {
        const int id = item.toInt(); auto row = results_[id].toMap();
        row.insert(field, value); results_[id] = row;
    }
    ++resultCategoryRevision_; emit resultCategoriesChanged();
}

bool AnalysisController::setResultRange(int id, double ls, double le, double rs, double re) {
    if (busy_ || exportBusy_ || reviewBusy_ || id < 0 || id >= results_.size() || id >= static_cast<int>(matches_.size())) return false;
    auto row = results_[id].toMap();
    const auto limit = [this, &row](const QString& side) {
        const auto info = sourceSummaries_.value(row.value(side + "Source").toString()).toMap();
        return info.value("durationSeconds", row.value("duration")).toDouble();
    };
    if (!std::isfinite(ls) || !std::isfinite(le) || !std::isfinite(rs) || !std::isfinite(re)
        || ls < 0 || rs < 0 || le <= ls || re <= rs || le > limit("left") || re > limit("right")) return false;
    pushReviewUndo({id}, true);
    if (!row.contains("originalRange")) {
        row.insert("originalRecord", row);
        row.insert("originalMatchDuration", matches_[id].durationSeconds);
        row.insert("originalRange", QVariantList{row.value("leftClipStart"), row.value("leftClipEnd"), row.value("rightClipStart"), row.value("rightClipEnd")});
    }
    row.insert("leftStart", ls); row.insert("leftEnd", le); row.insert("rightStart", rs); row.insert("rightEnd", re);
    row.insert("leftClipStart", ls); row.insert("leftClipEnd", le); row.insert("rightClipStart", rs); row.insert("rightClipEnd", re);
    row.insert("manualAdjusted", true);
    results_[id] = row;
    auto& match = matches_[id];
    match.leftStartSeconds = ls; match.leftEndSeconds = le; match.rightStartSeconds = rs; match.rightEndSeconds = re;
    // Exact custom bounds must also reach clip/XML/EDL exporters; original
    // scene bounds would otherwise clamp a manual adjustment away again.
    match.leftSceneStartSeconds = ls; match.leftSceneEndSeconds = le;
    match.rightSceneStartSeconds = rs; match.rightSceneEndSeconds = re;
    match.durationSeconds = std::min(le - ls, re - rs);
    ++resultCategoryRevision_; emit resultCategoriesChanged();
    return startReviewPreview(id, row, true);
}

bool AnalysisController::previewResultRange(int id, double leftStart, double rightStart) {
    if (busy_ || exportBusy_ || reviewBusy_ || id < 0 || id >= results_.size()
        || !std::isfinite(leftStart) || !std::isfinite(rightStart) || leftStart < 0 || rightStart < 0) return false;
    auto row = results_[id].toMap();
    const auto limit = [this, &row](const QString& side) { return sourceSummaries_.value(row.value(side + "Source").toString()).toMap().value("durationSeconds", row.value("duration")).toDouble(); };
    if (leftStart >= limit("left") || rightStart >= limit("right")) return false;
    row.insert("leftStart", leftStart); row.insert("rightStart", rightStart);
    return startReviewPreview(id, row, false);
}

bool AnalysisController::startReviewPreview(int id, const QVariantMap& row, bool publish) {
    reviewBusy_ = true; emit reviewBusyChanged();
    const QPointer<AnalysisController> guard(this);
    const auto previewDirectory = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/ParallelFinder-adjust-XXXXXX");
    reviewWorker_ = std::jthread([guard, row, id, previewDirectory, publish](std::stop_token stop) {
        QStringList previews; QString error;
        try {
            if (!previewDirectory->isValid()) throw std::runtime_error("Could not create preview folder");
            for (const QString& side : {QString("left"), QString("right")}) {
                if (stop.stop_requested()) break;
                pfcore::VideoDecoder decoder; pfcore::VideoDecodeOptions options; options.threads = 2;
                decoder.open(row.value(side + "Source").toString().toStdString(), options);
                const auto image = frameAt(decoder, row.value(side + "Start").toDouble(), stop);
                const QString path = previewDirectory->path() + '/' + side + ".png";
                if (!image.save(path)) throw std::runtime_error("Could not save preview");
                previews.push_back(QUrl::fromLocalFile(path).toString());
            }
        } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, row, id, previews, error, previewDirectory, publish] {
            if (!guard) return;
            if (publish && previews.size() == 2 && id < guard->results_.size()) {
                auto current = guard->results_[id].toMap();
                if (current.value("leftSource") == row.value("leftSource") && current.value("leftStart") == row.value("leftStart")) {
                    current.insert("leftPreview", previews[0]); current.insert("rightPreview", previews[1]);
                    guard->reviewPreviewDirectories_[id] = previewDirectory;
                    guard->results_[id] = current; ++guard->resultCategoryRevision_; emit guard->resultCategoriesChanged();
                }
            } else if (!publish) {
                guard->reviewDraftDirectory_ = previewDirectory;
                emit guard->reviewPreviewsReady(id, row.value("leftStart").toDouble(), row.value("rightStart").toDouble(), previews.value(0), previews.value(1));
            }
            guard->reviewBusy_ = false; emit guard->reviewBusyChanged();
            if (!error.isEmpty()) guard->setStatus(error);
        }, Qt::QueuedConnection);
    });
    return true;
}

bool AnalysisController::resetResultRange(int id) {
    if (busy_ || exportBusy_ || reviewBusy_ || id < 0 || id >= results_.size() || id >= static_cast<int>(matches_.size())) return false;
    const auto current = results_[id].toMap();
    auto original = current.value("originalRecord").toMap();
    if (original.isEmpty()) return false;
    pushReviewUndo({id}, true);
    for (const QString& key : {QString("category"), QString("categoryColor"), QString("favorite"), QString("hidden"), QString("reviewed")})
        if (current.contains(key)) original.insert(key, current.value(key));
    auto& match = matches_[id];
    match.leftStartSeconds = original.value("leftStart").toDouble(); match.leftEndSeconds = original.value("leftEnd").toDouble();
    match.rightStartSeconds = original.value("rightStart").toDouble(); match.rightEndSeconds = original.value("rightEnd").toDouble();
    match.leftSceneStartSeconds = original.value("leftSceneStart").toDouble(); match.leftSceneEndSeconds = original.value("leftSceneEnd").toDouble();
    match.rightSceneStartSeconds = original.value("rightSceneStart").toDouble(); match.rightSceneEndSeconds = original.value("rightSceneEnd").toDouble();
    match.durationSeconds = current.value("originalMatchDuration").toDouble();
    results_[id] = original; ++resultCategoryRevision_; emit resultCategoriesChanged();
    reviewPreviewDirectories_.erase(id);
    return true;
}

bool AnalysisController::exportFindings(const QString& output, const QVariantList& ids, int count) {
    if (busy_ || exportBusy_ || reviewBusy_ || (count != 1 && count != 3 && count != 5)) return false;
    const QString folder = localPath(output);
    if (folder.isEmpty() || !QDir().mkpath(folder)) return false;
    QVariantList rows; std::set<int> used;
    for (const auto& value : ids) {
        bool ok = false; const int id = value.toInt(&ok);
        if (!ok || id < 0 || id >= results_.size() || !used.insert(id).second) continue;
        const auto row = results_[id].toMap();
        if (!row.value("hidden").toBool()) rows.push_back(row);
    }
    if (rows.isEmpty()) return false;
    exportBusy_ = true; exportCompleted_ = 0; exportTotal_ = static_cast<int>(rows.size()); exportClipProgress_ = 0;
    emit exportBusyChanged(); emit exportProgressChanged();
    const bool ru = AppInfo::instance()->loadPreferences().value("language").toString() == "ru";
    const QString token = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    exportWorker_ = std::jthread([this, rows, folder, count, token, ru](std::stop_token stop) {
        QString error; int saved = 0;
        try {
            constexpr int width = 1920, margin = 32, pairHeight = 620, header = 92;
            for (int page = 0; page * 4 < rows.size(); ++page) {
                if (stop.stop_requested()) throw std::runtime_error(ru ? "Экспорт отменён" : "Export cancelled");
                const int pairs = std::min(4, static_cast<int>(rows.size()) - page * 4);
                QImage canvas(width, header + pairs * pairHeight + margin, QImage::Format_RGB32); canvas.fill(QColor("#0c0d10"));
                QPainter painter(&canvas); painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setPen(QColor("#f5f1ec")); painter.setFont(QFont("Segoe UI", 24, QFont::DemiBold));
                painter.drawText(QRect(margin, 20, width - 2 * margin, 40), ru ? "Лист находок" : "Parallel findings");
                painter.setFont(QFont("Segoe UI", 11)); painter.setPen(QColor("#a1a1aa"));
                painter.drawText(QRect(margin, 63, width - 2 * margin, 24), QString("Parallel Finder  ·  %1 / %2").arg(page + 1).arg((rows.size() + 3) / 4));
                for (int pair = 0; pair < pairs; ++pair) {
                    const auto row = rows[page * 4 + pair].toMap(); const int top = header + pair * pairHeight;
                    QColor pairColor(row.value("categoryColor").toString());
                    if (!pairColor.isValid()) pairColor = QColor("#df824d");
                    painter.setPen(pairColor); painter.drawLine(margin, top, width - margin, top);
                    painter.setFont(QFont("Segoe UI", 13, QFont::DemiBold)); painter.setPen(QColor("#f5f1ec"));
                    painter.drawText(QRect(margin, top + 12, width - 2 * margin, 30),
                        QString(ru ? "Пара %1  ·  Сходство %2%" : "Pair %1  ·  Similarity %2%")
                            .arg(row.value("id").toInt() + 1).arg(qRound(row.value("similarity").toDouble() * 100))
                            + (row.value("favorite").toBool() ? QStringLiteral("  ★") : QString{})
                            + (row.value("category").toString().isEmpty() ? QString{} : "  ·  " + row.value("category").toString()));
                    for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
                        const QString side = sideIndex == 0 ? "left" : "right", source = row.value(side + "Source").toString();
                        pfcore::VideoDecoder decoder; pfcore::VideoDecodeOptions options; options.threads = 2;
                        decoder.open(source.toStdString(), options);
                        const double start = row.value(side + "ClipStart", row.value(side + "Start")).toDouble();
                        const double end = row.value(side + "ClipEnd", row.value(side + "End")).toDouble();
                        painter.setFont(QFont("Segoe UI", 10)); painter.setPen(QColor("#a1a1aa"));
                        painter.drawText(QRect(margin, top + 48 + sideIndex * 276, width - 2 * margin, 24),
                            (sideIndex == 0 ? "A  ·  " : "B  ·  ") + QFileInfo(source).fileName());
                        const int cell = (width - 2 * margin - (count - 1) * 10) / count;
                        for (int column = 0; column < count; ++column) {
                            if (stop.stop_requested()) throw std::runtime_error(ru ? "Экспорт отменён" : "Export cancelled");
                            // End is exclusive; never sample the first frame of the next shot.
                            const double fps = std::max(1.0, decoder.info().frameRate);
                            const double seconds = start + std::max(0.0, end - start - 1.0 / fps) * (count == 1 ? 0 : double(column) / (count - 1));
                            double decodedTime = seconds;
                            const QImage image = frameAt(decoder, seconds, stop, &decodedTime);
                            const QRect area(margin + column * (cell + 10), top + 76 + sideIndex * 276, cell, 226);
                            painter.fillRect(area, QColor("#101114"));
                            const QSize fitted = image.size().scaled(area.size(), Qt::KeepAspectRatio);
                            painter.drawImage(QRect(area.center() - QPoint(fitted.width() / 2, fitted.height() / 2), fitted), image);
                            painter.setPen(QColor("#f5f1ec")); painter.drawText(QRect(area.x(), area.bottom() + 4, cell, 22), Qt::AlignCenter, timestamp(decodedTime));
                        }
                    }
                    const int completed = page * 4 + pair + 1;
                    QMetaObject::invokeMethod(this, [this, completed] { exportCompleted_ = completed; emit exportProgressChanged(); }, Qt::QueuedConnection);
                }
                painter.end();
                QSaveFile file(folder + "/ParallelFinder-findings-" + token + QString("-%1.png").arg(page + 1, 3, 10, QChar('0')));
                if (!file.open(QIODevice::WriteOnly) || !canvas.save(&file, "PNG") || !file.commit()) throw std::runtime_error("Could not write contact sheet");
                ++saved;
            }
        } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
        QMetaObject::invokeMethod(this, [this, error, saved, ru] {
            exportBusy_ = false; emit exportBusyChanged();
            emit exportFinished(error.isEmpty(), error.isEmpty() ? QString(ru ? "Сохранено изображений: %1" : "Images saved: %1").arg(saved) : error);
        }, Qt::QueuedConnection);
    });
    return true;
}
}
