#include <gtest/gtest.h>
#include <pfservices/ResultCategoryStore.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QSettings>

TEST(ResultCategories, TagsSurviveReloadAndReversedPairButNotChangedSource)
{
    int argc=1;char name[]="CategoryTests";char* argv[]={name,nullptr};
    QCoreApplication app(argc,argv);
    app.setOrganizationName("ParallelFinderTests");app.setApplicationName("CategoryTests");
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto a=directory.filePath("a.mp4"),b=directory.filePath("b.mp4");
    for(const auto& path:{a,b}) {QFile f(path);ASSERT_TRUE(f.open(QIODevice::WriteOnly));f.write("original");}
    QVariantMap pair{{"leftSource",a},{"rightSource",b},{"leftStart",1.25},{"leftEnd",2.5},{"rightStart",3.0},{"rightEnd",4.5}};
    pfservices::ResultCategoryStore::save(pair,"Head turns","#638edb");
    const auto loaded=pfservices::ResultCategoryStore::load(pair);
    EXPECT_EQ(loaded.value("name").toString(),QString("Head turns"));
    EXPECT_EQ(loaded.value("color").toString(),QString("#638edb"));
    QVariantMap reversed{{"leftSource",b},{"rightSource",a},{"leftStart",3.0},{"leftEnd",4.5},{"rightStart",1.25},{"rightEnd",2.5}};
    EXPECT_EQ(pfservices::ResultCategoryStore::load(reversed),loaded);
    pfservices::ResultCategoryStore::save(pair,"","");
    EXPECT_TRUE(pfservices::ResultCategoryStore::load(pair).isEmpty());
    pfservices::ResultCategoryStore::save(pair,"Body poses","#7c9885");
    const auto originalKey=pfservices::ResultCategoryStore::key(pair);
    QFile replaced(a);ASSERT_TRUE(replaced.open(QIODevice::WriteOnly));replaced.write("replacement-longer");replaced.close();
    EXPECT_TRUE(pfservices::ResultCategoryStore::load(pair).isEmpty());
    QSettings{}.remove(originalKey);
}
