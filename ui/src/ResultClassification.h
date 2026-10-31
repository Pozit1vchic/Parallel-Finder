#pragma once

#include <pfcore/MovementClassifier.hpp>
#include <QVariantMap>
#include <QStringList>
#include <algorithm>
#include <array>
#include "ScenePresentation.h"

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
    result.bodyOrientations = source.bodyOrientations;
    result.sourceId = source.sourceId;
    result.videoActions = source.videoActions;
    result.trackId=source.trackId;result.sceneIndex=source.sceneIndex;result.hasSceneIndex=source.hasSceneIndex;
    result.appearanceConfidence=source.appearanceConfidence;result.faceConfidence=source.faceConfidence;
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
    if(!row.value("manualPair").toBool())updateSceneEvidence(row,match,evidence[0],evidence[1]);
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
    QString movementClass = QString::fromStdString(match.gestureLabel + "/" + match.directionLabel);
    auto groupingActions=actions;
    if(movement.gesture==pfcore::GestureClass::HeadTurn || actions.contains("head_turn")) {
        const auto left=classifier.classifyHeadTurnForGrouping(evidence[0],match.leftStartSeconds,match.leftEndSeconds);
        const auto right=classifier.classifyHeadTurnForGrouping(evidence[1],match.rightStartSeconds,match.rightEndSeconds);
        // Different phases keep the broad class; never invent agreement
        // from the stronger side or change the admitted match itself.
        if(left==right) {
            if(movement.gesture==pfcore::GestureClass::HeadTurn)
                movementClass=QString::fromStdString(left+"/"+match.directionLabel);
            for(auto& action:groupingActions)if(action=="head_turn")action=QString::fromStdString(left);
        }
    }
    if(groupingActions.size()>1)movementClass=groupingActions.join('+')+"/"+QString::fromStdString(match.directionLabel);

    const auto posture = [&](int side) {
        if(match.headOnlyComparison)return pfcore::ScenePosture::Unknown;
        return pfcore::describeScene(evidence[side],
            side==0?match.leftStartSeconds:match.rightStartSeconds,
            side==0?match.leftEndSeconds:match.rightEndSeconds).posture;
    };
    using Posture=pfcore::ScenePosture;
    const auto leftPosture=posture(0),rightPosture=posture(1);
    if(leftPosture!=Posture::Unknown || rightPosture!=Posture::Unknown) {
        if(leftPosture==Posture::Unknown || rightPosture==Posture::Unknown)
            classification=QStringLiteral("posture_uncertain");
        else if(leftPosture==rightPosture) {
            switch(leftPosture) {
            case Posture::Sitting: classification=QStringLiteral("sitting");break;
            case Posture::Lying: classification=QStringLiteral("lying");break;
            case Posture::SeatedOrLying: classification=QStringLiteral("seated_lying");break;
            default:break; // Standing keeps its more specific movement/pose.
            }
        } else {
            const auto compatibleBroad=[](Posture a,Posture b) {
                return a==Posture::SeatedOrLying && (b==Posture::Sitting || b==Posture::Lying);
            };
            classification=compatibleBroad(leftPosture,rightPosture)||compatibleBroad(rightPosture,leftPosture)
                ? QStringLiteral("seated_lying") : QStringLiteral("posture_mismatch");
        }
        if(!classification.isEmpty()) {
            const bool dynamic=!(evidence[0].staticFrameSet && evidence[1].staticFrameSet);
            if(dynamic && movement.conflictingActions && classification!="posture_mismatch")
                classification=movement.headOnlyEvidence ? QStringLiteral("head_action_mismatch") : QStringLiteral("action_mismatch");
            else if(dynamic && (classification=="sitting" || classification=="lying") && !groupingActions.isEmpty())
                classification+='+'+groupingActions.join('+')+"/"+QString::fromStdString(match.directionLabel);
            row.insert("classificationConflict",movement.conflictingActions || classification=="posture_mismatch");
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
            row.insert("classificationConflict",true);
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
            row.insert("classification",(match.headOnlyComparison || movement.headOnlyEvidence)
                ? QStringLiteral("head_action_mismatch")
                : QStringLiteral("action_mismatch"));
            return;
        }
        classification=movementClass;
    }
    row.insert("classification", classification);
}
} // namespace pfui
