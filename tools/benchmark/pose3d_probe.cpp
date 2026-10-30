#include <pfgpu/Inference.hpp>
#include <pfcore/PathEncoding.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

// Research only: inspect and measure an externally supplied SimCC3D graph.
// No model download, source/cache mutation, or production policy is performed.
// Raw depth bins are retained until the author's camera/scale convention has
// been verified; they must not be silently interpreted as metric XYZ.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=2&&args.size()!=9&&args.size()!=10) {
        std::cerr<<"usage: pf_pose3d_probe MODEL [IMAGE LEFT TOP RIGHT BOTTOM rgb|bgr OUTPUT.json [cpu|cuda]]\n";return 2;
    }
    try {
        const auto start=std::chrono::steady_clock::now();
        pfgpu::SessionKey key;key.provider=args.size()==10&&args[9]=="cuda"?pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        key.intraOpThreads=4;key.profile="pose3d-research";
        const auto session=pfgpu::processSessionCache().getOrCreate(pfgpu::ModelRef::fromPath(pfcore::pathToUtf8(std::filesystem::path(args[1].toStdWString()))),key);
        if(!session.ok)throw std::runtime_error(session.error);
        const auto spec=pfgpu::describeSession(session.handle);if(!spec.ok)throw std::runtime_error(spec.error);
        const auto shape=[](const auto& values){QJsonArray out;for(auto v:values)out.append(static_cast<qint64>(v));return out;};
        QJsonArray outputs;for(const auto& out:spec.outputs)outputs.append(shape(out.shape));
        QJsonObject report{{"scope","research raw SimCC3D outputs; not validated body orientation"},{"input",shape(spec.input.shape)},{"outputs",outputs}};
        if(args.size()>2) {
            QImage image(args[2]);image=image.convertToFormat(QImage::Format_RGBA8888);if(image.isNull())throw std::runtime_error("Cannot load research frame");
            if(spec.input.shape!=std::vector<std::int64_t>{1,3,384,288})throw std::runtime_error("Expected fixed 288x384 RGB graph");
            const double left=args[3].toDouble(),top=args[4].toDouble(),right=args[5].toDouble(),bottom=args[6].toDouble();
            if(!std::isfinite(left+top+right+bottom)||right<=left||bottom<=top)throw std::runtime_error("Invalid research crop");
            const double cx=(left+right)/2,cy=(top+bottom)/2;
            double width=(right-left)*1.25,height=(bottom-top)*1.25;
            if(width/height>.75)height=width/.75;else width=height*.75;
            pfgpu::FloatTensor input;input.shape={1,3,384,288};input.values.resize(3*384*288);
            constexpr float mean[]={123.675F,116.28F,103.53F},stddev[]={58.395F,57.12F,57.375F};
            const auto pixel=[&](int x,int y,int c)->double {
                if(x<0||y<0||x>=image.width()||y>=image.height())return 0;
                return image.constScanLine(y)[x*4+c];
            };
            for(int y=0;y<384;++y)for(int x=0;x<288;++x) {
                const double sx=cx+(x-144.)*width/288.,sy=cy+(y-192.)*height/384.;
                const int ix=static_cast<int>(std::floor(sx)),iy=static_cast<int>(std::floor(sy));
                const double ax=sx-ix,ay=sy-iy;
                for(int c=0;c<3;++c) {
                    const int channel=args[7]=="bgr"?2-c:c;
                    const double value=(1-ay)*((1-ax)*pixel(ix,iy,channel)+ax*pixel(ix+1,iy,channel))
                        +ay*((1-ax)*pixel(ix,iy+1,channel)+ax*pixel(ix+1,iy+1,channel));
                    input.values[c*384*288+y*288+x]=(value-mean[c])/stddev[c];
                }
            }
            const auto inferenceStart=std::chrono::steady_clock::now();const auto result=pfgpu::runFloat(session.handle,input);
            if(!result.ok)throw std::runtime_error(result.error);
            report["inferenceMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-inferenceStart).count();
            if(result.outputs.size()!=3)throw std::runtime_error("Expected independent x/y/z SimCC tensors");
            for(const auto& out:result.outputs)if(out.shape.size()!=3||out.shape[0]!=1||out.shape[1]!=133)throw std::runtime_error("Expected 133 whole-body joints");
            QJsonArray joints,names;
            for(const auto& name:result.outputNames)names.append(QString::fromStdString(name));
            report["outputNames"]=names;
            for(int j=0;j<133;++j) {
                QJsonArray bins,peaks;
                for(const auto& out:result.outputs) {
                    const auto count=static_cast<std::size_t>(out.shape[2]);const auto first=out.values.begin()+j*count;
                    const auto peak=std::max_element(first,first+count);bins.append(static_cast<qint64>(peak-first));peaks.append(*peak);
                }
                const double x=bins[0].toDouble()*.5,y=bins[1].toDouble()*.5;
                joints.append(QJsonObject{{"index",j},{"bins",bins},{"peaks",peaks},
                    {"imageX",cx+(x-144.)*width/288.},{"imageY",cy+(y-192.)*height/384.}});
            }
            report["joints"]=joints;report["channelOrder"]=args[7];
        }
        report["totalMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const auto bytes=QJsonDocument(report).toJson();
        if(args.size()>2){QFile file(args[8]);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 3;}
        else std::cout<<bytes.constData();
    } catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
