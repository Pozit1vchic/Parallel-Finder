#pragma once
#include <QObject>
#include <QTcpServer>
#include <QJsonObject>
#include <functional>

namespace pfservices {
// Stateless Streamable HTTP: one bounded JSON-RPC message per POST, loopback
// only. Operations run on the owning application's thread via the handler.
class McpHttpServer final : public QObject {
public:
    using Handler=std::function<QJsonObject(const QJsonObject&)>;
    explicit McpHttpServer(Handler handler,QObject* parent=nullptr);
    bool start(quint16 port,const QByteArray& token,QString& error);
    void stop();
    quint16 port() const {return listener_.serverPort();}
    bool running() const {return listener_.isListening();}
private:
    QTcpServer listener_;
    Handler handler_;
    QByteArray token_;
    void accept();
};
}
