#include "AnalysisController.h"
#include "AppInfo.h"
#include "VideoActionReview.h"
#include <pfcore/VideoDecoder.hpp>
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
    while (!stop.stop_requested() && decoder.readNext(frame, false)) {
        if (frame.timestampSeconds + 1e-6 >= seconds) {
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
void clearPairVerification(QVariantMap& row, pfcore::MotionMatch& match) {
    match.similarity=match.rankScore=match.appearanceSimilarity=match.sceneSimilarity=match.dtwDistance=0;
    match.appearanceVerified=match.faceVerified=false;
    match.unmirroredSimilarity=-1;
    for (const auto key : {"similarity","rankScore","appearanceSimilarity","sceneSimilarity"}) row.insert(key,0.0);
    for (const auto key : {"identityVerified","faceVerified"}) row.insert(key,false);
}
}

void AnalysisController::pushReviewUndo(const QVariantList& ids, bool includeMatch) {
    std::vector<ReviewUndoRow> entry;
    std::set<int> used;
    for (const auto& value : ids) {
        const int id = value.toInt();
        if (id < 0 || id >= results_.size() || !used.insert(id).second) continue;
        ReviewUndoRow row{id, results_[id].toMap(), {}, {}, {}, {}};
        if (includeMatch && id < static_cast<int>(matches_.size())) row.match = matches_[id];
        if (includeMatch && id < static_cast<int>(resultPoseEvidence_.size())) row.evidence = resultPoseEvidence_[id];
        const auto preview = reviewPreviewDirectories_.find(id);
        if (preview != reviewPreviewDirectories_.end()) row.preview = preview->second;
        const auto originalPreview=reviewOriginalDirectories_.find(id);
        if(originalPreview!=reviewOriginalDirectories_.end()) row.originalPreview=originalPreview->second;
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
        results_[row.id] = row.record;
        if (row.match && row.id < static_cast<int>(matches_.size())) matches_[row.id] = *row.match;
        if (row.evidence && row.id < static_cast<int>(resultPoseEvidence_.size())) resultPoseEvidence_[row.id] = *row.evidence;
        if (row.preview) reviewPreviewDirectories_[row.id] = std::move(row.preview);
        else reviewPreviewDirectories_.erase(row.id);
        if(row.originalPreview) reviewOriginalDirectories_[row.id]=std::move(row.originalPreview);
        else reviewOriginalDirectories_.erase(row.id);
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

bool AnalysisController::setResultGroup(const QVariantList& ids, const QString& group) {
    if (busy_ || exportBusy_ || reviewBusy_ || group.size() > 80 || ids.isEmpty()) return false;
    const auto name = group.trimmed();
    for (const auto character : name) if (character.category() == QChar::Other_Control) return false;
    QVariantList changed;
    for (const auto& value : ids) {
        bool ok = false; const int id = value.toInt(&ok);
        if (!ok || id < 0 || id >= results_.size()) return false;
        if (ok && id >= 0 && id < results_.size() && !changed.contains(id)
            && results_[id].toMap().value("manualGroup").toString() != name) changed.append(id);
    }
    if (changed.isEmpty()) return true;
    pushReviewUndo(changed);
    for (const auto& value : changed) {
        auto row = results_[value.toInt()].toMap(); row.insert("manualGroup",name); results_[value.toInt()] = row;
    }
    ++resultCategoryRevision_; emit resultCategoriesChanged(); return true;
}

bool AnalysisController::removeResultSide(int id, const QString& side) {
    if (busy_ || exportBusy_ || reviewBusy_ || id < 0 || id >= results_.size()
        || id >= static_cast<int>(matches_.size()) || (side != "left" && side != "right")) return false;
    auto row = results_[id].toMap();
    const auto other = side == "left" ? QString("right") : QString("left");
    if (row.value(side + "Removed").toBool() || row.value(other + "Removed").toBool()) return false;
    pushReviewUndo({id},true);
    row.insert(side + "Removed",true); row.insert("manualPair",true); row.insert("reviewed",false);
    auto& match = matches_[id];
    (side == "left" ? match.leftEnabled : match.rightEnabled) = false;
    clearPairVerification(row,match);
    for (const auto key : {"similarity", "rankScore", "appearanceSimilarity", "sceneSimilarity"}) row.insert(key,0.0);
    for (const auto key : {"identityVerified", "faceVerified"}) row.insert(key,false);
    if (id < static_cast<int>(resultPoseEvidence_.size())) {
        auto effective = match;
        const auto& evidence = resultPoseEvidence_[id][other == "left" ? 0 : 1];
        effective.leftStartSeconds = effective.rightStartSeconds = row.value(other + "ClipStart").toDouble();
        effective.leftEndSeconds = effective.rightEndSeconds = row.value(other + "ClipEnd").toDouble();
        classifyResultRange(row,effective,{evidence,evidence});
    }
    match.durationSeconds = row.value(other + "ClipEnd").toDouble()-row.value(other + "ClipStart").toDouble();
    results_[id] = row; ++resultCategoryRevision_; emit resultCategoriesChanged(); return true;
}

bool AnalysisController::combineSingleResults(int targetId, int donorId) {
    if (targetId < 0 || donorId < 0 || targetId >= results_.size() || donorId >= results_.size()) return false;
    const auto target = results_[targetId].toMap(), donor = results_[donorId].toMap();
    if (target.value("leftRemoved").toBool() == target.value("rightRemoved").toBool()
        || donor.value("leftRemoved").toBool() == donor.value("rightRemoved").toBool()) return false;
    return replaceResultSide(targetId,target.value("leftRemoved").toBool()?"left":"right",
        donorId,donor.value("leftRemoved").toBool()?"right":"left",true);
}

bool AnalysisController::replaceResultSide(int targetId, const QString& targetSide, int donorId, const QString& donorSide, bool consumeDonor) {
    if (busy_ || exportBusy_ || reviewBusy_ || targetId == donorId
        || targetId < 0 || donorId < 0 || targetId >= results_.size() || donorId >= results_.size()
        || targetId >= static_cast<int>(matches_.size()) || donorId >= static_cast<int>(matches_.size())
        || (targetSide != "left" && targetSide != "right") || (donorSide != "left" && donorSide != "right")) return false;
    auto row = results_[targetId].toMap();
    const auto donor = results_[donorId].toMap();
    if (row.value("hidden").toBool() || donor.value("hidden").toBool() || donor.value(donorSide + "Removed").toBool()) return false;
    const auto other = targetSide == "left" ? QString("right") : QString("left");
    const double start = donor.value(donorSide + "ClipStart").toDouble();
    const double end = donor.value(donorSide + "ClipEnd").toDouble();
    if (donor.value(donorSide + "Source").toString().isEmpty() || !std::isfinite(start) || !std::isfinite(end) || end <= start) return false;
    // Reject replacing a side with the very same interval as its partner.
    if (donor.value(donorSide + "Source") == row.value(other + "Source")
        && std::abs(start - row.value(other + "ClipStart").toDouble()) < 1e-6
        && std::abs(end - row.value(other + "ClipEnd").toDouble()) < 1e-6) return false;
    if (consumeDonor && (row.value(targetSide + "Removed").toBool() == false
        || !donor.value((donorSide == "left" ? QString("right") : QString("left")) + "Removed").toBool())) return false;
    const auto ownedPreviews=std::make_shared<QTemporaryDir>(QDir::tempPath()+"/ParallelFinder-compose-XXXXXX");
    if(!ownedPreviews->isValid()) return false;
    for(const auto& side:{QString("left"),QString("right")}) {
        const auto path=side==targetSide ? donor.value(donorSide+"Preview").toString() : row.value(side+"Preview").toString();
        if(side!=targetSide && row.value(side+"Removed").toBool()) continue;
        if(!QFile::copy(localPath(path),ownedPreviews->filePath(side+".png"))) return false;
    }
    pushReviewUndo(consumeDonor ? QVariantList{targetId,donorId} : QVariantList{targetId}, true);
    for (const auto suffix : {"Source", "Preview", "TrackId", "Start", "End", "ClipStart", "ClipEnd", "SceneStart", "SceneEnd"})
        row.insert(targetSide + suffix, donor.value(donorSide + suffix));
    // Freeze both currently visible ranges: exporting a manual composition
    // must not apply the matcher's original automatic edge expansion again.
    for (const auto& side : {QString("left"), QString("right")}) {
        row.insert(side + "Start", row.value(side + "ClipStart"));
        row.insert(side + "End", row.value(side + "ClipEnd"));
        row.insert(side + "SceneStart", row.value(side + "ClipStart"));
        row.insert(side + "SceneEnd", row.value(side + "ClipEnd"));
    }
    auto& match = matches_[targetId];
    row.insert(targetSide + "Removed",false);
    for(const auto& side:{QString("left"),QString("right")})
        row.insert(side+"Preview",row.value(side+"Removed").toBool() ? QString{} : QUrl::fromLocalFile(ownedPreviews->filePath(side+".png")).toString());
    reviewPreviewDirectories_[targetId]=ownedPreviews;
    reviewOriginalDirectories_.erase(targetId);
    match.leftEnabled = !row.value("leftRemoved").toBool();
    match.rightEnabled = !row.value("rightRemoved").toBool();
    match.leftSourceId = row.value("leftSource").toString().toStdString();
    match.rightSourceId = row.value("rightSource").toString().toStdString();
    match.leftStartSeconds = match.leftSceneStartSeconds = row.value("leftClipStart").toDouble();
    match.leftEndSeconds = match.leftSceneEndSeconds = row.value("leftClipEnd").toDouble();
    match.rightStartSeconds = match.rightSceneStartSeconds = row.value("rightClipStart").toDouble();
    match.rightEndSeconds = match.rightSceneEndSeconds = row.value("rightClipEnd").toDouble();
    match.durationSeconds = std::min(match.leftEndSeconds-match.leftStartSeconds, match.rightEndSeconds-match.rightStartSeconds);
    clearPairVerification(row,match);
    match.appearanceVerified = match.faceVerified = match.headOnlyComparison = false;
    row.insert("manualPair", true); row.insert("manualAdjusted", true); row.insert("reviewed", false);
    for (const auto key : {"similarity", "rankScore", "appearanceSimilarity", "sceneSimilarity"}) row.insert(key, 0.0);
    for (const auto key : {"identityVerified", "faceVerified", "headOnlyComparison"}) row.insert(key, false);
    row.remove("originalRange"); row.remove("originalRecord"); row.remove("originalMatchDuration");
    row.insert("duration", std::max(match.leftEndSeconds, match.rightEndSeconds));
    row.insert("markers", QVariantList{match.leftStartSeconds,match.leftEndSeconds,match.rightStartSeconds,match.rightEndSeconds});
    if (targetId < static_cast<int>(resultPoseEvidence_.size()) && donorId < static_cast<int>(resultPoseEvidence_.size())) {
        resultPoseEvidence_[targetId][targetSide == "left" ? 0 : 1] = resultPoseEvidence_[donorId][donorSide == "left" ? 0 : 1];
        classifyResultRange(row, match, resultPoseEvidence_[targetId]);
    }
    results_[targetId] = row;
    if (consumeDonor) { auto consumed = donor; consumed.insert("hidden",true); results_[donorId] = consumed; }
    ++resultCategoryRevision_; emit resultCategoriesChanged();
    // Regenerate owned previews so later edits to the donor cannot invalidate
    // a temporary PNG still used by this composition.
    return startReviewPreview(targetId, row, true);
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
        if(auto existing=reviewPreviewDirectories_.find(id);existing!=reviewPreviewDirectories_.end()) reviewOriginalDirectories_[id]=existing->second;
        row.insert("originalRecord", row);
        row.insert("originalMatchDuration", matches_[id].durationSeconds);
        row.insert("originalRange", QVariantList{row.value("leftClipStart"), row.value("leftClipEnd"), row.value("rightClipStart"), row.value("rightClipEnd")});
    }
    row.insert("leftStart", ls); row.insert("leftEnd", le); row.insert("rightStart", rs); row.insert("rightEnd", re);
    row.insert("leftClipStart", ls); row.insert("leftClipEnd", le); row.insert("rightClipStart", rs); row.insert("rightClipEnd", re);
    row.insert("manualAdjusted", true);
    auto& match = matches_[id];
    match.leftStartSeconds = ls; match.leftEndSeconds = le; match.rightStartSeconds = rs; match.rightEndSeconds = re;
    // Exact custom bounds must also reach clip/XML/EDL exporters; original
    // scene bounds would otherwise clamp a manual adjustment away again.
    match.leftSceneStartSeconds = ls; match.leftSceneEndSeconds = le;
    match.rightSceneStartSeconds = rs; match.rightSceneEndSeconds = re;
    match.durationSeconds = std::min(le - ls, re - rs);
    if(id < static_cast<int>(resultPoseEvidence_.size()))
        classifyResultRange(row,match,resultPoseEvidence_[id]);
    results_[id] = row;
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
    std::optional<ResultPoseEvidence> actionEvidence;
    std::optional<pfcore::MotionMatch> actionMatch;
    const auto actionContext=videoActionContext_;
    if(publish && !row.value("classificationManual").toBool() && actionContext
        && id<static_cast<int>(resultPoseEvidence_.size()) && id<static_cast<int>(matches_.size())
        && !row.value("leftRemoved").toBool() && !row.value("rightRemoved").toBool()
        && !matches_[id].headOnlyComparison && row.value("matchType").toString()=="motion") {
        actionEvidence=resultPoseEvidence_[id];actionMatch=matches_[id];
    }
    reviewWorker_ = std::jthread([guard, row, id, previewDirectory, publish,actionContext,actionEvidence,actionMatch](std::stop_token stop) mutable {
        QStringList previews; QString error;
        bool actionsUpdated=false;
        try {
            if (!previewDirectory->isValid()) throw std::runtime_error("Could not create preview folder");
            for (const QString& side : {QString("left"), QString("right")}) {
                if (row.value(side + "Removed").toBool()) { previews.push_back({}); continue; }
                if (stop.stop_requested()) break;
                pfcore::VideoDecoder decoder; pfcore::VideoDecodeOptions options; options.threads = 2;
                decoder.open(row.value(side + "Source").toString().toStdString(), options);
                const auto image = frameAt(decoder, row.value(side + "Start").toDouble(), stop);
                const QString path = previewDirectory->path() + '/' + side + ".png";
                if (!image.save(path)) throw std::runtime_error("Could not save preview");
                previews.push_back(QUrl::fromLocalFile(path).toString());
            }
            if(actionEvidence && actionMatch && !stop.stop_requested()) {
                pfgpu::PoseEstimatorParams params;params.provider=actionContext->poseProvider;
                params.profile=actionContext->poseProfile;params.intraOpThreads=4;
                pfgpu::PoseEstimator pose(actionContext->poseModel.string(),params);
                auto match=*actionMatch;match.leftIndex=0;match.rightIndex=1;
                std::vector<pfcore::MotionWindow> windows{(*actionEvidence)[0],(*actionEvidence)[1]};
                pfcore::VideoDecodeOptions options;options.threads=2;
                const auto review=reviewMatchedVideoActions(windows,{match},pose,actionContext->poseModel,
                    actionContext->actionModel,actionContext->cacheDirectory,actionContext->sourceNamespaces,
                    actionContext->actionProvider,options,[&]{return stop.stop_requested();});
                if(!review.error.empty())error=QString::fromStdString(review.error);
                else if(!stop.stop_requested()) {
                    *actionEvidence={std::move(windows[0]),std::move(windows[1])};actionsUpdated=true;
                }
            }
        } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, row, id, previews, error, previewDirectory, publish,actionsUpdated,actionEvidence] {
            if (!guard) return;
            if(previews.size()!=2) {
                guard->reviewBusy_=false; emit guard->reviewBusyChanged();
                if(publish) guard->undoResultEdit();
                const auto message=error.isEmpty()?QStringLiteral("Could not decode frame"):error;
                guard->setStatus(message);
                emit guard->reviewPreviewFailed(id,row.value("leftStart").toDouble(),row.value("rightStart").toDouble(),message);
                return;
            }
            if (publish && previews.size() == 2 && id < guard->results_.size()) {
                auto current = guard->results_[id].toMap();
                bool sameRange=true;
                for(const auto key:{"leftSource","rightSource","leftStart","leftEnd","rightStart","rightEnd"})
                    if(current.value(key)!=row.value(key))sameRange=false;
                if (sameRange) {
                    if(actionsUpdated && actionEvidence && id<static_cast<int>(guard->resultPoseEvidence_.size())) {
                        for(int side=0;side<2;++side)for(const auto& evidence:(*actionEvidence)[side].videoActions) {
                            auto& history=guard->resultPoseEvidence_[id][side].videoActions;
                            std::erase_if(history,[&](const auto& old){return old.sourceId==evidence.sourceId
                                && old.start==evidence.start && old.end==evidence.end;});
                            history.push_back(evidence);
                            if(history.size()>32)history.erase(history.begin()+1);
                        }
                        classifyResultRange(current,guard->matches_[id],guard->resultPoseEvidence_[id]);
                    }
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
    for (const QString& key : {QString("favorite"), QString("hidden"), QString("reviewed"), QString("manualGroup"), QString("manualPair"), QString("leftRemoved"), QString("rightRemoved")})
        if (current.contains(key)) original.insert(key, current.value(key));
    if(current.value("classificationManual").toBool()) {
        original.insert("classification",current.value("classification"));
        original.insert("classificationManual",true);
    }
    auto& match = matches_[id];
    match.leftStartSeconds = original.value("leftStart").toDouble(); match.leftEndSeconds = original.value("leftEnd").toDouble();
    match.rightStartSeconds = original.value("rightStart").toDouble(); match.rightEndSeconds = original.value("rightEnd").toDouble();
    match.leftSceneStartSeconds = original.value("leftSceneStart").toDouble(); match.leftSceneEndSeconds = original.value("leftSceneEnd").toDouble();
    match.rightSceneStartSeconds = original.value("rightSceneStart").toDouble(); match.rightSceneEndSeconds = original.value("rightSceneEnd").toDouble();
    match.durationSeconds = current.value("originalMatchDuration").toDouble();
    if (current.value("manualPair").toBool()) {
        clearPairVerification(original,match);
        original.insert("headOnlyComparison",current.value("headOnlyComparison"));
        match.headOnlyComparison=current.value("headOnlyComparison").toBool();
        if (match.leftEnabled != match.rightEnabled) match.durationSeconds = match.leftEnabled
            ? match.leftEndSeconds-match.leftStartSeconds : match.rightEndSeconds-match.rightStartSeconds;
    }
    if(id < static_cast<int>(resultPoseEvidence_.size()))
        classifyResultRange(original,match,resultPoseEvidence_[id]);
    results_[id] = original; ++resultCategoryRevision_; emit resultCategoriesChanged();
    if(auto preview=reviewOriginalDirectories_.find(id);preview!=reviewOriginalDirectories_.end()) {
        reviewPreviewDirectories_[id]=preview->second; reviewOriginalDirectories_.erase(preview);
    } else reviewPreviewDirectories_.erase(id);
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
                    painter.setPen(QColor("#df824d")); painter.drawLine(margin, top, width - margin, top);
                    painter.setFont(QFont("Segoe UI", 13, QFont::DemiBold)); painter.setPen(QColor("#f5f1ec"));
                    painter.drawText(QRect(margin, top + 12, width - 2 * margin, 30),
                        (row.value("manualPair").toBool()
                            ? QString(ru ? "Результат %1 · Собран вручную" : "Result %1 · Manually composed").arg(row.value("id").toInt()+1)
                            : QString(ru ? "Пара %1  ·  Сходство %2%" : "Pair %1  ·  Similarity %2%")
                                .arg(row.value("id").toInt() + 1).arg(qRound(row.value("similarity").toDouble() * 100)))
                            + (row.value("favorite").toBool() ? QStringLiteral("  ★") : QString{})
                            + "  ·  " + row.value("classification", "unknown").toString());
                    for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
                        const QString side = sideIndex == 0 ? "left" : "right", source = row.value(side + "Source").toString();
                        if (row.value(side + "Removed").toBool()) continue;
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
