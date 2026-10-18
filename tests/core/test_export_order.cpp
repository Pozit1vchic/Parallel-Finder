#include "../../ui/src/ExportOrder.h"
#include <gtest/gtest.h>

TEST(ExportOrder, MissingGroupsPrecedeMachineClassesButExplicitOrderIsKept)
{
    const QVariantList records{
        QVariantMap{{"manualGroup","Zeta"}}, QVariantMap{{"manualGroup","Alpha"}},
        QVariantMap{{"manualGroup","Hidden"},{"hidden",true}}, QVariantMap{{"classification","step"}}};
    const QVariantList selected{0,1,2,3};
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"step","unknown"},selected),
        (QStringList{"group:Alpha","group:Zeta","step"}));
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"step","group:Zeta","unknown"},selected),
        (QStringList{"group:Alpha","step","group:Zeta"}));
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"group:Zeta","group:Alpha","step"},selected),
        (QStringList{"group:Zeta","group:Alpha","step"}));
    EXPECT_TRUE(pfui::effectiveExportClassOrder(
        QVariantList{QVariantMap{{"classification","step"}},QVariantMap{{"classification","wave"}}}, {}, {0}).isEmpty());
}

TEST(ExportOrder, UnselectedGroupsDoNotShiftSelectedFolderPrefixes)
{
    const QVariantList records{
        QVariantMap{{"manualGroup","Not selected"}},
        QVariantMap{{"classification","head_turn_away"}},
        QVariantMap{{"classification","step"}},
        QVariantMap{{"manualGroup","Also not selected"}}};
    const QStringList classifierOrder{"head_turn_away","head_turn_back","head_turn","turn",
        "head_front","head_turned","head_pose","arms_raised","arms_extended","arms_folded",
        "legs_bent","legs_straight","body_pose","pose_mismatch","head_nod","head_tilt",
        "raise","lower","raise_lower","wave","push","pull","reach_return","step","static","mixed","unknown"};

    // The caller supplies the complete classifier order but exports only the
    // selected step result. Previously, the unselected manual groups were
    // prepended and caused this class to be named 03_step instead of 01_step.
    const auto stepOnly = pfui::effectiveExportClassOrder(records,classifierOrder,{2});
    EXPECT_EQ(stepOnly,QStringList{"step"});
    EXPECT_EQ(stepOnly.indexOf("step")+1,1);

    // UI's default order is manual groups first, then classifier classes;
    // the export helper must produce that same sequence for selected rows.
    const QVariantList mixed{
        QVariantMap{{"manualGroup","Editorial"}},
        QVariantMap{{"classification","step"}},
        QVariantMap{{"manualGroup","Hidden from this export"}},
        QVariantMap{{"classification","head_turn_away"}}};
    const auto uiDefault = pfui::effectiveExportClassOrder(mixed,classifierOrder,{0,1,3});
    EXPECT_EQ(uiDefault,(QStringList{"group:Editorial","head_turn_away","step"}));
    const auto userOrder = pfui::effectiveExportClassOrder(mixed,
        {"step","group:Editorial","head_turn_away"},{0,1,3});
    EXPECT_EQ(userOrder,(QStringList{"step","group:Editorial","head_turn_away"}));
}

TEST(ExportOrder, GroupFolderPreservesUnicodeAndAvoidsUnsafeNamesAndCollisions)
{
    const auto cyrillic=QString::fromUtf8("Поворот головы от камеры");
    const auto folder=pfui::exportGroupFileName(cyrillic);
    EXPECT_TRUE(folder.startsWith("group_"+cyrillic+"-"));
    EXPECT_NE(pfui::exportGroupFileName("a/b"),pfui::exportGroupFileName("a_b"));
    EXPECT_NE(pfui::exportGroupFileName("Alpha"),pfui::exportGroupFileName("alpha"));
    EXPECT_TRUE(pfui::exportGroupFileName("CON").startsWith("group_CON-"));
    const auto unsafe=pfui::exportGroupFileName(" .<>:\"/\\|?*\n");
    for(const auto character:QStringLiteral("<>:\"/\\|?*\n")) EXPECT_FALSE(unsafe.contains(character));
    EXPECT_FALSE(unsafe.endsWith('.'));
    EXPECT_FALSE(unsafe.endsWith(' '));
    const auto longName=QString(80,QChar(0x0416));
    EXPECT_EQ(pfui::exportGroupFileName(longName).toUcs4().size(),6+48+1+8);
    const auto emoji=QString::fromUtf8("😀");
    QString manyEmoji;for(int i=0;i<70;++i)manyEmoji+=emoji;
    EXPECT_EQ(pfui::exportGroupFileName(manyEmoji).toUcs4().size(),6+48+1+8);
}

TEST(ExportOrder, SingleRightSideUsesItsOwnSourceAndTime)
{
    std::vector<pfcore::MotionMatch> matches(3);
    matches[0].leftEnabled=false; matches[0].leftSourceId="z-unused";
    matches[0].leftStartSeconds=100; matches[0].rightSourceId="a-source"; matches[0].rightStartSeconds=1;
    matches[1].leftSourceId="a-source";matches[1].leftStartSeconds=2;
    matches[2].leftSourceId="a-source";matches[2].leftStartSeconds=0;
    const QVariantList records{QVariantMap{{"manualGroup","Turn"}},QVariantMap{{"manualGroup","Turn"}},QVariantMap{{"classification","step"}}};
    EXPECT_EQ(pfui::orderedExportIndexes({0,1,2},matches,records,0,{}),(std::vector<int>{2,0,1}));
    const auto order=pfui::effectiveExportClassOrder(records,{"step","unknown"},{0,1,2});
    EXPECT_EQ(pfui::orderedExportIndexes({0,1,2},matches,records,0,order),(std::vector<int>{0,1,2}));
    EXPECT_EQ(pfui::exportGroupKey(QVariantMap{{"manualGroup","turn/away"}}),QString("group:turn/away"));
}
