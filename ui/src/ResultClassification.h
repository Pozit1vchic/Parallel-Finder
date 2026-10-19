#pragma once

#include <pfcore/MovementClassifier.hpp>
#include <QVariantMap>
#include <QStringList>
#include <algorithm>
#include <array>

namespace pfui {
using ResultPoseEvidence = std::array<pfcore::MotionWindow, 2>;

// Keep measured poses for range edits without retaining image descriptors,
// identity prototypes, or a second copy of the analysis cache.
inline pfcore::MotionWindow groupingEvidence(const pfcore::MotionWindow& source) {
    pfcore::MotionWindow result;
    result.frames = source.frames;
    result.sceneStartSeconds = source.sceneStartSeconds;
    result.sceneEndSeconds = source.sceneEndSeconds;
    result.staticFrameSet = source.staticFrameSet;
    result.headOrientations = source.headOrientations;
    result.sourceId = source.sourceId;
    result.videoActions = source.videoActions;
    return result;
}

inline void classifyResultRange(QVariantMap& row, pfcore::MotionMatch& match,
                                const ResultPoseEvidence& evidence) {
    if (match.leftEnabled != match.rightEnabled) {
        auto single = match;
        const bool left = match.leftEnabled;
        single.leftEnabled = single.rightEnabled = true;
        single.leftStartSeconds = single.rightStartSeconds = left ? match.leftStartSeconds : match.rightStartSeconds;
        single.leftEndSeconds = single.rightEndSeconds = left ? match.leftEndSeconds : match.rightEndSeconds;
        const auto& observed = evidence[left ? 0 : 1];
        classifyResultRange(row,single,{observed,observed});
        match.directionLabel=single.directionLabel; match.gestureLabel=single.gestureLabel;
        return;
    }
    const pfcore::MovementClassifier classifier;
    const auto a = classifier.classifyForGrouping(evidence[0], match.leftStartSeconds,
                                                  match.leftEndSeconds, match.headOnlyComparison);
    const auto b = classifier.classifyForGrouping(evidence[1], match.rightStartSeconds,
                                                  match.rightEndSeconds, match.headOnlyComparison);
    const auto movement = pfcore::MovementClassifier::combineForGrouping(a,b);
    match.directionLabel = pfcore::MovementClassifier::directionName(movement.direction);
    match.gestureLabel = pfcore::MovementClassifier::gestureName(movement.gesture);
    row.insert("direction", QString::fromStdString(match.directionLabel));
    row.insert("gesture", QString::fromStdString(match.gestureLabel));
    QStringList actions;
    for(const auto action:movement.actions)
        actions.push_back(QString::fromLatin1(pfcore::MovementClassifier::gestureName(action)));
    row.insert("classificationActions",actions);
    row.insert("classificationConflict",movement.conflictingActions);
    row.insert("classificationPartial",match.headOnlyComparison || movement.headOnlyEvidence);
    if (row.value("classificationManual").toBool()) return;
    QString classification;
    const auto leftPosture=match.headOnlyComparison ? std::string("unknown")
        : classifier.classifyPostureForGrouping(evidence[0],match.leftStartSeconds,match.leftEndSeconds);
    const auto rightPosture=match.headOnlyComparison ? std::string("unknown")
        : classifier.classifyPostureForGrouping(evidence[1],match.rightStartSeconds,match.rightEndSeconds);
    if(leftPosture!="unknown" || rightPosture!="unknown") {
        if(leftPosture=="seated_lying" && rightPosture=="seated_lying")
            classification=QStringLiteral("seated_lying");
        else if(leftPosture=="standing" && rightPosture=="standing") {
            // Keep the more specific movement/pose grouping for two confirmed
            // standing clips.
        } else if(leftPosture!="unknown" && rightPosture!="unknown")
            classification=QStringLiteral("posture_mismatch");
        else
            classification=QStringLiteral("posture_uncertain");
        if(!classification.isEmpty()) {
            row.insert("classification",classification);
            return;
        }
    }
    if (evidence[0].staticFrameSet && evidence[1].staticFrameSet) {
        const auto left = classifier.classifyPoseForGrouping(evidence[0], match.leftStartSeconds,
                                                              match.leftEndSeconds, match.headOnlyComparison);
        const auto right = classifier.classifyPoseForGrouping(evidence[1], match.rightStartSeconds,
                                                               match.rightEndSeconds, match.headOnlyComparison);
        const std::string fallback = match.headOnlyComparison ? "head_pose" : "body_pose";
        if (left == right) {
            classification = QString::fromStdString(left);
        } else if (left != fallback && right != fallback) {
            // Static evidence can be confidently different on each side. Keep
            // that disagreement visible and sorted together, with both poses
            // shown as detail instead of silently calling it a neutral pose.
            const auto poses = std::minmax(left, right);
            classification = QStringLiteral("pose_mismatch/%1+%2")
                .arg(QString::fromStdString(poses.first), QString::fromStdString(poses.second));
        } else {
            // A fallback label means at least one side lacks enough evidence
            // for a specific pose; do not turn uncertainty into a mismatch.
            classification = QString::fromStdString(fallback);
        }
    } else {
        if(movement.conflictingActions) {
            row.insert("classification",QStringLiteral("action_mismatch"));
            return;
        }
        classification = QString::fromStdString(match.gestureLabel + "/" + match.directionLabel);
        auto groupingActions=actions;
        if(movement.gesture==pfcore::GestureClass::HeadTurn || actions.contains("head_turn")) {
            const auto left=classifier.classifyHeadTurnForGrouping(evidence[0],match.leftStartSeconds,match.leftEndSeconds);
            const auto right=classifier.classifyHeadTurnForGrouping(evidence[1],match.rightStartSeconds,match.rightEndSeconds);
            // Different phases keep the broad class; never invent agreement
            // from the stronger side or change the admitted match itself.
            if(left==right) {
                if(movement.gesture==pfcore::GestureClass::HeadTurn)
                    classification=QString::fromStdString(left+"/"+match.directionLabel);
                for(auto& action:groupingActions)if(action=="head_turn")action=QString::fromStdString(left);
            }
        }
        if(groupingActions.size()>1)classification=groupingActions.join('+')+"/"+QString::fromStdString(match.directionLabel);
    }
    row.insert("classification", classification);
}
} // namespace pfui
