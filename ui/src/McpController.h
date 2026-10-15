#pragma once
#include <QObject>
#include <QJsonArray>
#include <pfservices/McpHttpServer.hpp>

namespace pfui {
class AnalysisController;
class McpController final:public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool allowControl READ allowControl WRITE setAllowControl NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
public:
    explicit McpController(AnalysisController* analysis,const QString& settingsPath={},QObject* parent=nullptr);
    bool enabled()const{return enabled_;}
    bool allowControl()const{return allowControl_;}
    int port()const{return port_;}
    bool running()const{return server_.running();}
    QString error()const{return error_;}
    QString endpoint()const{return QString("http://127.0.0.1:%1/mcp").arg(port_);}
    void setEnabled(bool value);
    void setAllowControl(bool value);
    void setPort(int value);
    Q_INVOKABLE void copyConnection();
    Q_INVOKABLE void rotateKey();
    QJsonObject dispatch(const QJsonObject& request);
signals:
    void changed();
private:
    AnalysisController* analysis_;
    QString settingsPath_,error_,token_;
    QJsonValue exportSuccess_{QJsonValue::Null};
    QString exportMessage_;
    QString resultSet_;
    QJsonObject draftPreview_;
    bool enabled_=false,allowControl_=false;
    int port_=32145;
    pfservices::McpHttpServer server_;
    void apply();
    void persist();
    QJsonArray extendedTools() const;
    QJsonObject callExtendedTool(const QString& name,const QJsonObject& arguments,QJsonArray& images);
};
}
