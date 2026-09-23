#include "command/commandDispatcher.hpp"
#include "protocol/respEncoder.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace{

    constexpr size_t kActiveExpireSampleCount = 64;
    constexpr size_t kAofRewriteMinSize = 65536;
    using CommandHandler = std::string(*)(InMemoryDB& db,const std::vector<std::string>& argv);

    struct CommandSpec{
        int arity;      // 正数 = 恰好;负数 = 至少 |arity|(含命令名)
        bool isWrite;
        CommandHandler handler;
    };

    struct MGetValue{
        bool exists;
        std::string value;
    };

    std::string wrongArity(const std::string& cmd) {
        return RespEncoder::error("ERR wrong number of arguments for '" + cmd + "' command");
    }
    std::string wrongTypeReply() {
        return RespEncoder::error("WRONGTYPE Operation against a key holding the wrong kind of value");
    }
    std::string okReply() {
        return RespEncoder::simpleString("OK");
    }
    std::string intReply(long long value) {
        return RespEncoder::integer(value);
    }
    std::string nilReply() {
        return RespEncoder::nullBulk();
    }

    bool arityOk(int arity,size_t argc){
        if(arity >= 0) return argc == static_cast<size_t>(arity);
        return argc >= static_cast<size_t>(-arity);
    }

    bool toLongLong(const std::string& s,long long& out){
        try{
            size_t parsed = 0;
            out = std::stoll(s,&parsed,10);
            return parsed == s.size();
        }catch(const std::exception&){
            return false;
        }
    }

    std::string encodeMGetReply(const std::vector<MGetValue>& values) {
        std::string out = "*" + std::to_string(values.size()) + "\r\n";
        for (const MGetValue& v : values) {
            if (!v.exists) {
                out += nilReply();
            } else {
                out += RespEncoder::bulkString(v.value);
            }
        }
        return out;
    }

    std::string handlePing(InMemoryDB& db,const std::vector<std::string>& argv){
        return RespEncoder::simpleString("PONG");
    }

    std::string handleSet(InMemoryDB& db,const std::vector<std::string>& argv){
        db.set(argv[1],argv[2]);
        return okReply();
    }

    std::string handleMset(InMemoryDB& db,const std::vector<std::string>& argv){
        if(argv.size()%2==0) return wrongArity("mset");
        for(size_t i = 1; i + 1 < argv.size(); i+=2){
            db.set(argv[i],argv[i+1]);
        }
        return okReply();
    }

    std::string handleGet(InMemoryDB& db,const std::vector<std::string>& argv){
        std::string value;
        const DBStatus status = db.getStringValue(argv[1],value);

        if(status == DBStatus::WrongType) return wrongTypeReply();
        if(status != DBStatus::OK) return nilReply();

        return RespEncoder::bulkString(value);
    }

    std::string handleMGet(InMemoryDB& db,const std::vector<std::string>& argv){
        std::vector<MGetValue> values;
        values.reserve(argv.size()-1);
        for(size_t i = 1; i<argv.size(); i++){
            std::string value;
            if(db.getStringValue(argv[i],value)!=DBStatus::OK){
                values.push_back(MGetValue{false,std::move(value)});
            }else{
                values.push_back(MGetValue{true,std::move(value)});
            }
        }

        return encodeMGetReply(values);
    }

    std::string handleDel(InMemoryDB& db,const std::vector<std::string>& argv){
        long long removed = 0;
        for(size_t i = 1; i<argv.size(); i++){
            removed += db.del(argv[i]);
        }
        return intReply(removed);
    }

    std::string handleExists(InMemoryDB& db,const std::vector<std::string>& argv){
        long long exists = 0;
        for(size_t i = 1; i<argv.size(); i++){
            exists += db.exists(argv[i])?1:0;
        }
        return intReply(exists);
    }

    std::string handleIncr(InMemoryDB& db,const std::vector<std::string>& argv){
        long long newValue = 0;
        std::string err;
        if(!db.incr(argv[1],newValue,err)) return RespEncoder::error(err);
        return intReply(newValue);
    }

    std::string handleIncrBy(InMemoryDB& db,const std::vector<std::string>& argv){
        long long delta = 0;
        if(!toLongLong(argv[2],delta)){
            return RespEncoder::error("ERR value is not an integer or out of range");
        }
        long long newValue = 0;
        std::string err;
        if(!db.incrBy(argv[1],delta,newValue,err)) return RespEncoder::error(err);
        return intReply(newValue);
    }

    std::string handleHset(InMemoryDB& db,const std::vector<std::string>& argv){
        if(argv.size()%2!=0) return wrongArity("hset");

        std::vector<std::pair<std::string,std::string>> fieldValues;
        fieldValues.reserve((argv.size()-2)/2);
        for(size_t i = 2; i + 1 < argv.size(); i += 2){
            fieldValues.emplace_back(argv[i],argv[i+1]);
        }

        int added = 0;
        const DBStatus status = db.hset(argv[1],fieldValues,added);
        if(status == DBStatus::WrongType) return wrongTypeReply();
        return intReply(added);
    }

    std::string handleHget(InMemoryDB& db,const std::vector<std::string>& argv){
        std::string value;
        const DBStatus status = db.hget(argv[1],argv[2],value);
        if(status == DBStatus::WrongType) return wrongTypeReply();
        if(status != DBStatus::OK) return nilReply();
        return RespEncoder::bulkString(value);
    }

    std::string handleHdel(InMemoryDB& db,const std::vector<std::string>& argv){
        std::vector<std::string> fields(argv.begin()+2,argv.end());
        int removed = 0;
        const DBStatus status = db.hdel(argv[1],fields,removed);
        if(status == DBStatus::WrongType) return wrongTypeReply();
        if(status != DBStatus::OK) return intReply(0);
        return intReply(removed);
    }

    std::string handleHexists(InMemoryDB& db,const std::vector<std::string>& argv){
        bool exists = false;
        const DBStatus status = db.hexists(argv[1],argv[2],exists);
        if(status == DBStatus::WrongType) return wrongTypeReply();
        return intReply(status == DBStatus::OK && exists ? 1 : 0);
    }

    std::string handleHlen(InMemoryDB& db,const std::vector<std::string>& argv){
        size_t len = 0;
        const DBStatus status = db.hlen(argv[1],len);
        if(status == DBStatus::WrongType) return wrongTypeReply();
        if(status != DBStatus::OK) return intReply(0);
        return intReply(static_cast<long long>(len));
    }

    std::string handleHgetall(InMemoryDB& db,const std::vector<std::string>& argv){
        std::vector<DBHashFieldEntry> entries;
        const DBStatus status = db.hgetall(argv[1],entries);
        if(status == DBStatus::WrongType) return wrongTypeReply();

        std::vector<std::string> flat;
        flat.reserve(entries.size()*2);
        for(const DBHashFieldEntry& entry : entries){
            flat.push_back(entry.key);
            flat.push_back(entry.value);
        }
        return RespEncoder::array(flat);
    }

    std::string handleExpire(InMemoryDB& db,const std::vector<std::string>& argv){
        long long seconds = 0;
        if(!toLongLong(argv[2],seconds)){
            return RespEncoder::error("ERR value is not an integer or out of range");
        }
        return intReply(db.expire(argv[1],seconds));
    }

    std::string handleTtl(InMemoryDB& db,const std::vector<std::string>& argv){
        return intReply(db.ttl(argv[1]));
    }

    std::string handlePttl(InMemoryDB& db,const std::vector<std::string>& argv){
        return intReply(db.pttl(argv[1]));
    }

    std::string handlePersist(InMemoryDB& db,const std::vector<std::string>& argv){
        return intReply(db.persist(argv[1]));
    }

    // 分数转字符串:to_string 是定点 6 位小数(1e-7 会变 "0.000000"),这里要最短往返表示
    std::string scoreToString(double score){
        char buf[32];
        const auto result = std::to_chars(buf,buf + sizeof(buf),score);
        return std::string(buf,result.ptr);
    }

    const std::unordered_map<std::string_view, CommandSpec> kCommands = {
        {"ping",    {-1, false, handlePing}},
        {"set",     {3,  true,  handleSet}},
        {"mset",    {-3, true,  handleMset}},
        {"get",     {2,  false, handleGet}},
        {"mget",    {-2, false, handleMGet}},
        {"del",     {-2, true,  handleDel}},
        {"exists",  {-2, false, handleExists}},
        {"incr",    {2,  true,  handleIncr}},
        {"incrby",  {3,  true,  handleIncrBy}},
        {"hset",    {-4, true,  handleHset}},
        {"hget",    {3,  false, handleHget}},
        {"hdel",    {-3, true,  handleHdel}},
        {"hexists", {3,  false, handleHexists}},
        {"hlen",    {2,  false, handleHlen}},
        {"hgetall", {2,  false, handleHgetall}},
        {"expire",  {3,  true,  handleExpire}},
        {"ttl",     {2,  false, handleTtl}},
        {"pttl",    {2,  false, handlePttl}},
        {"persist", {2,  true,  handlePersist}},
    };
}

std::string CommandDispatcher::dispatch(const std::vector<std::string>& argv){
    return dispatchInternal(argv,false);
}
std::string CommandDispatcher::dispatchInternal(const std::vector<std::string>& argv, bool replayingAof){
    if(argv.empty()) return RespEncoder::error("ERR empty command");

    // 命令名大小写不敏感:统一转小写再查表
    std::string name = argv[0];
    for(char& c : name){
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if(name == "rewriteaof"){
        std::string err;
        if(!aof_.rewriteCommands(snapshotCommands(),err)){
            return RespEncoder::error(err);
        }
        return RespEncoder::simpleString("rewriting started");
    }
    else if(name == "bgrewriteaof"){
        std::string err;
        if(!aof_.startBackgroundRewrite(snapshotCommands(),err)){
            return RespEncoder::error(err);
        }
        return RespEncoder::simpleString("Background append only file rewriting started");
    }

    const auto it = kCommands.find(name);
    if(it == kCommands.end()){
        return RespEncoder::error("ERR unknown command '" + argv[0] + "'");
    }
    const CommandSpec& spec = it->second;

    if(!arityOk(spec.arity,argv.size())) return wrongArity(name);

    const std::string message = spec.handler(db_,argv);

    if(spec.isWrite && !replayingAof && (!message.empty()&&message[0]!='-')){
        // 写命令执行成功后才追加进 AOF;失败的命令和回放中的命令都不写入
        std::string err;
        if(!aof_.appendCommand(argv,err)){
            return RespEncoder::error(err);
        }
    }

    return message;
}

bool CommandDispatcher::loadAof(){
    lastError_.clear();
    return aof_.replay(
        [&](const std::vector<std::string>& argv,std::string& err){
            const std::string replay = dispatchInternal(argv,true);
            if(!replay.empty()&&replay[0]=='-'){
                err = replay;
                return false;
            }
            return true;
        },
    lastError_);
}

bool CommandDispatcher::rewriteAof(std::string& err){
    err.clear();
    return aof_.rewriteCommands(snapshotCommands(),err);
}
bool CommandDispatcher::backgroundRewriteAof(std::string& err){
    err.clear();
    return aof_.startBackgroundRewrite(snapshotCommands(),err);
}
std::vector<std::vector<std::string>> CommandDispatcher::snapshotCommands(){
    std::vector<DBSnapshotEntry> entries = db_.snapshot();
    std::vector<std::vector<std::string> > commands;
    commands.reserve(entries.size()*2);
    for(const DBSnapshotEntry& entry : entries){
        int64_t ttlMs = -1;
        if(entry.expiredAtMs > 0){
            ttlMs = entry.expiredAtMs - InMemoryDB::nowMs();
            if(ttlMs <= 0) continue;        // 已死：整条跳过
        }

        if(entry.type == redisObject::RedisObjectType::STRING){
            commands.push_back({"SET",entry.key,entry.stringValue});
        }else if(entry.type == redisObject::RedisObjectType::HASH){
            if(!entry.hashEntries.empty()){
                std::vector<std::string> argv;
                argv.reserve(2 + entry.hashEntries.size()*2);
                argv.push_back("HSET");
                argv.push_back(entry.key);
                for(const auto& element : entry.hashEntries){
                    argv.push_back(element.key);
                    argv.push_back(element.value);
                }
                commands.push_back(std::move(argv));
            }
        }else if(entry.type == redisObject::RedisObjectType::ZSET){
            if(!entry.zsetEntries.empty()){
                std::vector<std::string> argv;
                argv.reserve(2 + entry.zsetEntries.size()*2);
                argv.push_back("ZADD");
                argv.push_back(entry.key);
                for(const auto& element : entry.zsetEntries){
                    argv.push_back(scoreToString(element.score));
                    argv.push_back(element.member);
                }
                commands.push_back(std::move(argv));
            }
        }

        if(ttlMs > 0){
            commands.push_back({"EXPIRE", entry.key, std::to_string((ttlMs + 999) / 1000)});
        }
    }

    return commands;
}

void CommandDispatcher::cron(){
    (void)db_.activeExpireCycle(kActiveExpireSampleCount);

    std::string err;
    if (!aof_.flushIfNeeded(err)) {
        lastError_ = "AOF fsync failed: " + err;
    }
    const bool wasBackgroundRewrite = aof_.backgroundRewriteInProgress();
    if (!aof_.pollBackgroundRewrite(err)) {
        lastError_ = "AOF background rewrite failed: " + err;
    }
    else if(wasBackgroundRewrite && !aof_.backgroundRewriteInProgress()){
        lastRewriteSize_ = aof_.fileSize(err);
    }
    if(!aof_.backgroundRewriteInProgress() 
        && aof_.fileSize(err)>kAofRewriteMinSize
        && aof_.fileSize(err)>lastRewriteSize_*2)
        {
            if(!aof_.startBackgroundRewrite(snapshotCommands(),err)){
                lastError_ = "AOF BackgroundRewrite failed: " + err;
            }
        }
}
