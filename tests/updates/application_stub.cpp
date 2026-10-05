#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QSaveFile>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();const auto root=app.applicationDirPath();
    if(args.contains("--pf-update-healthcheck"))return QFile::exists(root+"/fail-health.txt")?6:0;
    const auto index=args.indexOf("--pf-update-ack");
    if(index>=0 && index+1<args.size()) {
        if(QFile::exists(root+"/fail-restart.txt"))return 7;
        QFile version(root+"/version.txt");if(!version.open(QIODevice::ReadOnly))return 8;
        QSaveFile ack(args[index+1]);if(ack.open(QIODevice::WriteOnly)){ack.write(version.readAll());ack.commit();}
        QThread::msleep(500);return 0;
    }
    if(args.contains("--wait"))QThread::msleep(1000);
    return 0;
}
