#include "../../ui/src/ExportOrder.h"
#include <gtest/gtest.h>

TEST(ExportOrder, MissingGroupsPrecedeMachineClassesButExplicitOrderIsKept)
{
    const QVariantList records{
        QVariantMap{{"manualGroup","Zeta"}}, QVariantMap{{"manualGroup","Alpha"}},
        QVariantMap{{"manualGroup","Hidden"},{"hidden",true}}, QVariantMap{{"classification","step"}}};
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"step","unknown"}),
        (QStringList{"group:Alpha","group:Zeta","step","unknown"}));
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"step","group:Zeta","unknown"}),
        (QStringList{"group:Alpha","step","group:Zeta","unknown"}));
    EXPECT_EQ(pfui::effectiveExportClassOrder(records,{"group:Zeta","group:Alpha","step"}),
        (QStringList{"group:Zeta","group:Alpha","step"}));
    EXPECT_TRUE(pfui::effectiveExportClassOrder({QVariantMap{{"classification","step"}}},{}).isEmpty());
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
    const auto order=pfui::effectiveExportClassOrder(records,{"step","unknown"});
    EXPECT_EQ(pfui::orderedExportIndexes({0,1,2},matches,records,0,order),(std::vector<int>{0,1,2}));
    EXPECT_EQ(pfui::exportGroupKey(QVariantMap{{"manualGroup","turn/away"}}),QString("group:turn/away"));
}
