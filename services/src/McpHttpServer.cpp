#include <pfservices/McpHttpServer.hpp>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <QHash>
#include <memory>
#include <algorithm>

namespace pfservices {
namespace {
constexpr qsizetype headerLimit=16384,bodyLimit=1024*1024;
void respond(QTcpSocket* socket,int status,const QByteArray& body={}) {
    const QByteArray reason=status==200?"OK":status==202?"Accepted":status==400?"Bad Request":
        status==401?"Unauthorized":status==403?"Forbidden":status==404?"Not Found":
        status==405?"Method Not Allowed":status==406?"Not Acceptable":status==413?"Payload Too Large":"Internal Server Error";
    socket->write("HTTP/1.1 "+QByteArray::number(status)+" "+reason+"\r\nConnection: close\r\n"
        "Content-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: "+QByteArray::number(body.size())
        +(status==401?"\r\nWWW-Authenticate: Bearer":"")
        +(status==405?"\r\nAllow: POST":"")+"\r\n\r\n"+body);
    socket->disconnectFromHost();
}
bool equalsSecret(const QByteArray& a,const QByteArray& b) {
    if(a.size()!=b.size())return false;
    unsigned difference=0;for(qsizetype i=0;i<a.size();++i)difference|=unsigned(static_cast<unsigned char>(a[i])^static_cast<unsigned char>(b[i]));
    return difference==0;
}
}
McpHttpServer::McpHttpServer(Handler handler,QObject* parent):QObject(parent),handler_(std::move(handler)) {
    connect(&listener_,&QTcpServer::newConnection,this,&McpHttpServer::accept);
}
bool McpHttpServer::start(quint16 port,const QByteArray& token,QString& error) {
    stop();error.clear();
    if(token.size()<32){error="MCP access key is missing";return false;}
    token_=token;
    if(!listener_.listen(QHostAddress::LocalHost,port)){error=listener_.errorString();token_.clear();return false;}
    return true;
}
void McpHttpServer::stop() {
    listener_.close();token_.clear();
    for(auto* socket:findChildren<QTcpSocket*>())socket->abort();
}
void McpHttpServer::accept() {
    while(listener_.hasPendingConnections()) {
        auto* socket=listener_.nextPendingConnection();socket->setParent(this);
        if(findChildren<QTcpSocket*>().size()>32){socket->abort();socket->deleteLater();continue;}
        socket->setReadBufferSize(headerLimit+bodyLimit+1);
        connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        auto* deadline=new QTimer(socket);deadline->setSingleShot(true);deadline->start(10000);
        connect(deadline,&QTimer::timeout,socket,&QTcpSocket::abort);
        auto bytes=std::make_shared<QByteArray>();
        auto handled=std::make_shared<bool>(false);
        connect(socket,&QTcpSocket::readyRead,this,[this,socket,bytes,handled,deadline] {
            if(*handled)return;
            bytes->append(socket->readAll());
            // Keep a deadline while the response drains too: a client that
            // stops reading must not occupy a connection indefinitely.
            const auto finish=[&](int status,const QByteArray& body=QByteArray{}){*handled=true;deadline->start(10000);respond(socket,status,body);};
            const auto edge=bytes->indexOf("\r\n\r\n");
            if(edge<0){if(bytes->size()>headerLimit)finish(413);return;}
            if(edge>headerLimit){finish(413);return;}
            const auto lines=bytes->left(edge).split('\n');
            const auto request=lines.front().trimmed().split(' ');
            if(request.size()!=3 || request[2]!="HTTP/1.1"){finish(400);return;}
            QHash<QByteArray,QByteArray> headers;
            for(qsizetype i=1;i<lines.size();++i) {
                const auto line=lines[i].trimmed();const auto colon=line.indexOf(':');
                if(colon<=0 || headers.contains(line.left(colon).toLower())){finish(400);return;}
                headers.insert(line.left(colon).toLower(),line.mid(colon+1).trimmed());
            }
            const auto authority=QByteArray("127.0.0.1:")+QByteArray::number(port());
            const auto localAuthority=QByteArray("localhost:")+QByteArray::number(port());
            if(headers.value("host")!=authority && headers.value("host")!=localAuthority){finish(403);return;}
            if(headers.contains("origin") && headers.value("origin")!="http://"+authority
                && headers.value("origin")!="http://"+localAuthority){finish(403);return;}
            if(!equalsSecret(headers.value("authorization"),"Bearer "+token_)){finish(401);return;}
            if(request[1]!="/mcp"){finish(404);return;}
            if(request[0]!="POST"){finish(405);return;}
            const auto accept=headers.value("accept").toLower();
            if(!accept.contains("application/json") || !accept.contains("text/event-stream")){finish(406);return;}
            const auto version=headers.value("mcp-protocol-version");
            if(!version.isEmpty() && version!="2025-06-18" && version!="2025-11-25"){finish(400);return;}
            if(headers.contains("transfer-encoding") || !headers.value("content-type").toLower().startsWith("application/json")){finish(400);return;}
            const auto lengthText=headers.value("content-length");
            if(lengthText.isEmpty() || !std::all_of(lengthText.begin(),lengthText.end(),[](char c){return c>='0'&&c<='9';})){finish(400);return;}
            bool lengthOk=false;const auto length=lengthText.toLongLong(&lengthOk);
            if(!lengthOk || length<0){finish(400);return;}
            if(length>bodyLimit){finish(413);return;}
            if(bytes->size()<edge+4+length)return;
            if(bytes->size()!=edge+4+length){finish(400);return;}
            QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes->mid(edge+4,length),&parse);
            if(parse.error!=QJsonParseError::NoError || !document.isObject()){finish(400);return;}
            const auto message=document.object();
            if(version.isEmpty() && message["method"]!="initialize"){finish(400);return;}
            if(message["jsonrpc"]!="2.0" || !message["method"].isString()
                || (message.contains("params") && !message["params"].isObject())
                || (message.contains("id") && !message["id"].isString() && !message["id"].isDouble())){finish(400);return;}
            if(!message.contains("id")){finish(202);return;}
            try {finish(200,QJsonDocument(handler_(message)).toJson(QJsonDocument::Compact));}
            catch(...){finish(500);}
        });
    }
}
}
