#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <pfservices/PfCache.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>

namespace {
std::string digest(std::span<const std::uint8_t> bytes) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(bytes.data()),
        static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha256).toHex().toStdString();
}
struct Primary {std::string key,hash;};
}

// Destructive only to an explicitly marked disposable cache COPY. It never
// opens source videos, calls models, or claims complete analysis performance.
int main(int argc,char** argv) {
    if(argc!=4){std::cerr<<"Usage: pf_cache_budget_probe DISPOSABLE_CACHE LIMIT_MiB OUTPUT.json\n";return 2;}
    try {
        const auto root=std::filesystem::absolute(argv[1]);
        const auto output=std::filesystem::absolute(argv[3]);
        if(std::filesystem::is_symlink(root) || !std::filesystem::is_directory(root)
            || std::filesystem::exists(output))return 3;
        std::ifstream marker(root/".pf-disposable-benchmark");std::string permission;
        std::getline(marker,permission);
        if(permission!="ParallelFinder disposable benchmark snapshot v1")return 3;
        std::size_t consumed=0;const auto mib=std::stoull(argv[2],&consumed);
        if(consumed!=std::string(argv[2]).size() || !mib || mib>64ULL*1024)return 3;
        pfservices::PfCache cache(root,static_cast<std::size_t>(mib*1024*1024));
        std::vector<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::directory_iterator(root))
            if(!entry.is_symlink() && entry.is_regular_file() && entry.path().extension()==".pfc")files.push_back(entry.path());
        std::sort(files.begin(),files.end());
        std::vector<Primary> primary;
        for(const auto& path:files) {
            std::ifstream input(path,std::ios::binary);std::array<char,8> magic{};
            std::uint32_t version=0,keySize=0;std::uint64_t size=0;
            input.read(magic.data(),magic.size());input.read(reinterpret_cast<char*>(&version),4);
            input.read(reinterpret_cast<char*>(&keySize),4);input.read(reinterpret_cast<char*>(&size),8);
            if(!input || std::string(magic.data(),magic.size())!="PFCACHE1" || version!=1
                || !keySize || keySize>16384 || size>512ULL*1024*1024
                || std::filesystem::file_size(path)!=24+keySize+size)continue;
            std::string key(keySize,'\0');input.read(key.data(),keySize);
            if(!input || (!key.starts_with("motion-v") && !key.starts_with("scene-sequence-v")))continue;
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            if(!input)return 4;
            // Direct read intentionally does not promote LRU timestamps before
            // testing the policy on the snapshot's actual access ordering.
            primary.push_back({std::move(key),digest(bytes)});
        }
        if(primary.empty())return 4;
        const auto before=cache.bytesUsed();std::string error;
        const bool admitted=cache.put("preview-v1|disposable-capacity-probe",{0},error);
        const auto after=cache.bytesUsed();QJsonArray records;std::size_t retained=0;
        for(const auto& item:primary) {
            const auto bytes=cache.get(item.key);
            const bool unchanged=bytes && digest(*bytes)==item.hash;
            retained+=unchanged;
            const auto keyBytes=QByteArray::fromStdString(item.key);
            records.append(QJsonObject{{"keySha256",QString::fromLatin1(QCryptographicHash::hash(keyBytes,QCryptographicHash::Sha256).toHex())},
                {"payloadSha256",QString::fromStdString(item.hash)},{"retainedUnchanged",unchanged}});
        }
        const QJsonObject report{{"scope","Actual observation cache COPY under reduced PFC budget; no model or matcher run"},
            {"limitBytes",static_cast<qint64>(cache.limitBytes())},{"beforePfcBytes",static_cast<qint64>(before)},
            {"afterPfcBytes",static_cast<qint64>(after)},{"triggerAdmitted",admitted},{"triggerError",QString::fromStdString(error)},
            {"primaryRecords",static_cast<qint64>(primary.size())},{"retainedPrimaryRecords",static_cast<qint64>(retained)},
            {"primary",records}};
        QFile file(QString::fromStdWString(output.wstring()));if(!file.open(QIODevice::WriteOnly))return 5;
        const auto encoded=QJsonDocument(report).toJson();if(file.write(encoded)!=encoded.size())return 5;
        std::cout<<"Primary records retained unchanged: "<<retained<<'/'<<primary.size()<<"; PFC bytes "<<before<<" -> "<<after<<'\n';
        return after<=cache.limitBytes() ? 0:6;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 7;}
}
