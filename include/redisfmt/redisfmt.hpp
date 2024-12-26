#ifndef __REDISFMT_H__
#define __REDISFMT_H__

#include <map>
#include <optional>
#include <vector>
#include <chrono>
#include <array>

#include "hiredis.h"

#include "fmt/format.h"
#include "tl/expected.hpp"

namespace rdsfmt {

#ifndef LOG_ERROR
#define LOG_ERROR(...)
#define LOG_WARN(...)
#define LOG_INFO(...)
#endif

constexpr std::string_view kRedisNilStr{ "(nil)" };

struct RedisInitParam {
    std::string host;
    int port = 0;
    std::string auth;
    int context_count = 0;
    int db_index = 0;
    bool use_ssl = false;
    int connect_timeout = 0;
    int heart_invervals = 0;
};

class RedisReply {
public:
    RedisReply(void* reply)
        : reply_(static_cast<redisReply*>(reply), freeReplyObject) {}
    RedisReply(const RedisReply& other) : reply_(other.reply_) {}
    std::shared_ptr<redisReply> operator->() { return reply_; }
    operator bool() { return reply_ != nullptr; }
    operator redisReply* () { return reply_.get(); }

private:
    std::shared_ptr<redisReply> reply_;
};

namespace detail {
template<typename T, typename = void>
struct is_container : std::false_type {};

template<typename T>
struct is_container<T, std::void_t<typename T::value_type, typename T::iterator>> : std::true_type {};


template<typename T, typename = void>
struct is_pair : std::false_type {};

template<typename T>
struct is_pair<T, std::void_t<typename T::first_type, typename T::second_type>> : std::true_type {};
}

namespace RedisOp {

//template <std::size_t N> struct Option {
//    consteval Option(const char(&str)[N]) {
//        for (std::size_t i = 0; i < N; ++i) {
//            value[i] = str[i];
//        }
//    }
//
//    char value[N];
//};
//template <std::size_t N> Option(const char(&str)[N]) -> Option<N>;

template<const char* op, typename T>
struct RedisOptions {
    RedisOptions(T v) : value(std::forward<T>(v)) {}

    constexpr static std::string_view option{ op };
    std::optional<T> value;
};

template<const char* op>
struct RedisOptions<op, void> {
    constexpr static std::string_view option{ op };
};

constexpr char OptionStrEX[] = "EX";
using EX = RedisOptions<OptionStrEX, int64_t>;

constexpr char OptionStrPX[] = "PX";
using PX = RedisOptions<OptionStrPX, int64_t>;

constexpr char OptionStrEXAT[] = "EXAT";
using EXAT = RedisOptions<OptionStrEXAT, int64_t>;

constexpr char OptionStrPXAT[] = "PXAT";
using PXAT = RedisOptions<OptionStrPXAT, int64_t>;

constexpr char OptionStrNX[] = "NX";
using NX = RedisOptions<OptionStrNX, void>;

constexpr char OptionStrXX[] = "XX";
using XX = RedisOptions<OptionStrXX, void>;

constexpr char OptionStrKEEPTTL[] = "KEEPTTL";
using KEEPTTL = RedisOptions<OptionStrKEEPTTL, void>;

constexpr char OptionStrGET[] = "GET";
using GET = RedisOptions<OptionStrGET, void>;

}




/*
Use redisReply* instead of RedisReply to avoid a double free error when the type
is [REDIS_REPLY_ARRAY, REDIS_REPLY_MAP...] in the recursive call of
GetFromReply.
*/
template <typename T> tl::expected<T, int> GetFromReply(redisReply* reply);

template <int RT, typename T, typename = void> struct RedisReplyConvert;
#define REDIS_ARRAY_CONVERT_REPLY(data_type) \
template <> struct RedisReplyConvert<REDIS_REPLY_ARRAY, data_type> { \
static tl::expected <data_type, int> Convert(redisReply* reply) { \
    if (reply->elements == 0) { \
        LOG_ERROR("%s, reply->elements=0 is invalid", __FUNCTION__); \
        return tl::unexpected{ -1 }; \
    } \
    else if (reply->elements % 2 != 0) { \
        LOG_ERROR("%s, reply->elements=%lld is invalid", __FUNCTION__, reply->elements); \
        return tl::unexpected{ -1 }; \
    } \
    data_type data; \
    auto reflection = GetReflection(data); \
    auto& field_map = reflection.GetFieldMap(); \
    for (int i = 0, e = reply->elements; i < e; i += 2) { \
        if (auto iter = field_map.find(reply->element[i]->str); iter != field_map.end()) { \
            if (iter->second->type == ReflectionTypeId<std::string>::TypeId()) { \
                reflection.SetValue(reply->element[i]->str, std::string(reply->element[i + 1]->str)); \
            } \
            else if (iter->second->type == ReflectionTypeId<int64_t>::TypeId()) { \
                reflection.SetValue<int64_t>(reply->element[i]->str, common_func::stoll(reply->element[i + 1]->str).value_or(0)); \
            } \
            else { \
                reflection.SetValue(reply->element[i]->str, common_func::stoi(reply->element[i + 1]->str).value_or(0)); \
            } \
        } \
        else { \
            LOG_ERROR("%s has no field: %s", #data_type, reply->element[i]->str); \
        } \
    } \
    return data; \
} \
}


/*
check if the type T has a convert function
tmeplate<int RT, typename T> tl::expected<T, int> RedisReplyConvert(redisReply*);
*/
template <int RT, typename T, typename = void>
struct has_convert_function : std::false_type {};
template <int RT, typename T>
struct has_convert_function<
    RT, T,
    std::void_t<decltype(std::declval<RedisReplyConvert<RT, T>>().Convert(
        std::declval<redisReply*>()))>>
    : std::is_same<decltype(std::declval<RedisReplyConvert<RT, T>>().Convert(
        std::declval<redisReply*>())),
    tl::expected<T, int>> {};

template <int RT, typename T, typename = void>
struct is_redis_reply_convertible : std::false_type {};

template <int RT, typename T>
struct is_redis_reply_convertible<
    RT, T, std::enable_if_t<has_convert_function<RT, T>::value>>
    : public std::true_type {};

template <> struct RedisReplyConvert<REDIS_REPLY_INTEGER, int> {
    static inline tl::expected<int, int> Convert(redisReply* reply) {
        return static_cast<int>(reply->integer);
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_INTEGER, size_t> {
    static inline tl::expected<size_t, int> Convert(redisReply* reply) {
        return reply->integer;
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_INTEGER, std::string> {
    static inline tl::expected<std::string, int> Convert(redisReply* reply) {
        return std::to_string(reply->integer);
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_STRING, std::string> {
    static inline tl::expected<std::string, int> Convert(redisReply* reply) {
        return std::string(reply->str);
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_STRING, int> {
    static inline tl::expected<int, int> Convert(redisReply* reply) {
        try {
            return std::stoi(reply->str);
        }
        catch (std::invalid_argument const& e) {
            return tl::unexpected{ -1 };
        }
        return tl::unexpected{ -1 };
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_STRING, int64_t> {
    static inline tl::expected<int64_t, int> Convert(redisReply* reply) {
        try {
            return std::stol(reply->str);
        }
        catch (std::invalid_argument const& e) {
            return tl::unexpected{ -1 };
        }
        return tl::unexpected{ -1 };
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_STRING, unsigned long> {
    static inline tl::expected<unsigned long, int> Convert(redisReply* reply) {
        try {
            return std::stol(reply->str);
        }
        catch (std::invalid_argument const& e) {
            return tl::unexpected{ -1 };
        }
        return tl::unexpected{ -1 };
    }
};

template <typename T>
struct RedisReplyConvert<
    REDIS_REPLY_ARRAY, T,
    std::enable_if_t<detail::is_container<T>::value&&
    detail::is_pair<typename T::value_type>::value>> {
    using K = typename T::value_type::first_type;
    using V = typename T::value_type::second_type;
    static inline tl::expected<T, int> Convert(redisReply* reply) {
        T result;

        auto inserter = std::inserter(result, result.end());
        for (size_t i = 0, e = reply->elements; i < e; i += 2) {
            auto k = GetFromReply<std::decay_t<K>>(reply->element[i]);
            auto v = GetFromReply<std::decay_t<V>>(reply->element[i + 1]);
            if (k && v) {
                *inserter++ =
                    std::make_pair<K, V>(std::move(k.value()), std::move(v.value()));
            }
        }
        return std::move(result);
    }
};

template <typename T>
struct RedisReplyConvert<
    REDIS_REPLY_ARRAY, T,
    std::enable_if_t<detail::is_container<T>::value &&
    !detail::is_pair<typename T::value_type>::value>> {
    using V = typename T::value_type;
    static inline tl::expected<T, int> Convert(redisReply* reply) {
        T result;
        auto inserter = std::inserter(result, result.end());
        for (size_t i = 0, e = reply->elements; i < e; i++) {
            auto v = GetFromReply<std::decay_t<V>>(reply->element[i]);
            if (v)
                *inserter++ = v.value();
            else if constexpr (std::is_same_v<V, std::string>)
                *inserter++ = std::string(kRedisNilStr);
        }
        return std::move(result);
    }
};

template <typename T>
struct RedisReplyConvert<
    REDIS_REPLY_ARRAY, T, std::enable_if_t<detail::is_pair<T>::value>> {
    using K = typename T::first_type;
    using V = typename T::second_type;
    static inline tl::expected<T, int> Convert(redisReply* reply) {
        if (reply->elements < 2) return tl::unexpected{ -1 };
        auto k = GetFromReply<std::decay_t<K>>(reply->element[0]);
        auto v = GetFromReply<std::decay_t<V>>(reply->element[1]);
        if (k && v)
            return std::make_pair<K, V>(std::move(k.value()), std::move(v.value()));
        else
			LOG_INFO("k or v is invalid");
        return tl::unexpected{ -1 };
    }
};

template <> struct RedisReplyConvert<REDIS_REPLY_STATUS, std::string> {
    static inline tl::expected<std::string, int> Convert(redisReply* reply) {
        return std::string(reply->str);
    }
};

template <typename T> tl::expected<T, int> GetFromReply(redisReply* reply) {
    if (!reply) {
        LOG_ERROR("no redis reply");
        return tl::unexpected{ -1 };
    }

    switch (reply->type) {
    case REDIS_REPLY_STRING:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_STRING, T>::value)
            return RedisReplyConvert<REDIS_REPLY_STRING, T>::Convert(reply);
        break;

    case REDIS_REPLY_ARRAY:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_ARRAY, T>::value)
            return RedisReplyConvert<REDIS_REPLY_ARRAY, T>::Convert(reply);
        break;

    case REDIS_REPLY_INTEGER:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_INTEGER, T>::value)
            return RedisReplyConvert<REDIS_REPLY_INTEGER, T>::Convert(reply);
        break;

    case REDIS_REPLY_NIL:
        return tl::unexpected{ REDIS_REPLY_NIL };

    case REDIS_REPLY_STATUS:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_STATUS, T>::value)
            return RedisReplyConvert<REDIS_REPLY_STATUS, T>::Convert(reply);
        break;

    case REDIS_REPLY_ERROR:
        LOG_ERROR("redis replay error:%s", reply->str);
        return tl::unexpected{ REDIS_REPLY_ERROR };

    case REDIS_REPLY_DOUBLE:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_DOUBLE, T>::value)
            return RedisReplyConvert<REDIS_REPLY_DOUBLE, T>::Convert(reply);
        break;

    case REDIS_REPLY_BOOL:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_BOOL, T>::value)
            return RedisReplyConvert<REDIS_REPLY_BOOL, T>::Convert(reply);
        break;

    case REDIS_REPLY_MAP:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_MAP, T>::value)
            return RedisReplyConvert<REDIS_REPLY_MAP, T>::Convert(reply);
        break;

    case REDIS_REPLY_SET:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_SET, T>::value)
            return RedisReplyConvert<REDIS_REPLY_SET, T>::Convert(reply);
        break;

    case REDIS_REPLY_ATTR:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_ATTR, T>::value)
            return RedisReplyConvert<REDIS_REPLY_ATTR, T>::Convert(reply);
        break;

    case REDIS_REPLY_PUSH:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_PUSH, T>::value)
            return RedisReplyConvert<REDIS_REPLY_PUSH, T>::Convert(reply);
        break;

    case REDIS_REPLY_BIGNUM:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_BIGNUM, T>::value)
            return RedisReplyConvert<REDIS_REPLY_BIGNUM, T>::Convert(reply);
        break;

    case REDIS_REPLY_VERB:
        if constexpr (is_redis_reply_convertible<REDIS_REPLY_VERB, T>::value)
            return RedisReplyConvert<REDIS_REPLY_VERB, T>::Convert(reply);
        break;
    }

#ifdef DEBUG
    LOG_ERROR("%s: type[%d] not convertible, %s", __FUNCTION__, reply->type,
        __PRETTY_FUNCTION__);
#endif // DEBUG

    return tl::unexpected{ -1 };
}

class RedisMgr {
public:
    RedisMgr() {}
    virtual ~RedisMgr() { 
        UnInit();
    }

    template<typename ...Args>
    int Initialize(Args&&... args) {
        (redis_cxt_pool_.push_back(args), ...);
        return 0;
    }
    void UnInit() {
        for (auto& context : redis_cxt_pool_)
            redisFree(context);
    }

    tl::expected<std::string, int> AUTH(std::string_view password) {
        static std::string cmd = GetCmd("AUTH", 1);
        return ExcuteCommand<std::string>(cmd, std::string(password));
    }

    tl::expected<std::string, int> SELECT(int index) {
        static std::string cmd = GetCmd("SELECT", 1);
        return ExcuteCommand<std::string>(cmd, fmt::format("{}", index));
    }

    template <typename T, typename F>
    tl::expected<T, int> HGET(std::string_view key, const F& field) {
        static_assert(is_redis_reply_convertible<REDIS_REPLY_STRING, T>::value,
            "no function RedisReplyConvert<REDIS_REPLY_STRING, T>::Convert can be called.");
        static std::string cmd = GetCmd("HGET", 2);
        return ExcuteCommand<T>(cmd, std::string(key), fmt::format("{}", field));
    }

    /*
    a list of values associated with the given fields, in the same order as they are requested.
    if any of requested is not exists, then the associated value is '(nil)'
    */
    template <typename ...Field>
    tl::expected<std::vector<std::string>, int> HMGET(std::string_view key, Field... field) {
        constexpr size_t arg_count = sizeof...(field);
        static_assert(arg_count > 0, "invalid number of arguement");
        static std::string cmd = GetCmd("HMGET", arg_count + 1);
        return ExcuteCommand<std::vector<std::string>>(cmd, std::string(key), fmt::format("{}", field)...);
    }


    template <typename T>
    tl::expected<T, int> HGETALL(std::string_view key) {
        static_assert(is_redis_reply_convertible<REDIS_REPLY_ARRAY, T>::value,
            "no function RedisReplyConvert<REDIS_REPLY_ARRAY, T>::Convert can be called.");
        static std::string cmd = GetCmd("HGETALL", 1);
        return ExcuteCommand<T>(cmd, std::string(key));
    }

    // Integer reply: the value of the field after the increment operation.
    template <typename T>
    tl::expected<int, int> HINCRBY(std::string_view key, const T& field, int inc) {
        static std::string cmd = GetCmd("HINCRBY", 3);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", field), fmt::format("{}", inc));
    }

    template <typename... Args>
    tl::expected<int, int> HSET(std::string_view key, Args &&...args) {
        constexpr size_t arg_count = sizeof...(args);
        static_assert(arg_count % 2 == 0 && arg_count > 0, "invalid number of arguement");
        static std::string cmd = GetCmd("HSET", arg_count + 1);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", args)...);
    }

    template <typename T>
    tl::expected<int, int> HSET(std::string_view key, T&& arg, 
        std::enable_if_t<detail::is_container<std::decay_t<T>>::value && detail::is_pair<typename std::decay_t<T>::value_type>::value, int> = 0) {
        std::string cmd = fmt::format("HSET {}", key);
        for (auto& iter : arg)
            cmd += fmt::format(" {} {}", iter.first, iter.second);
        return ExcuteCommand<int>(cmd);
    }

    tl::expected<int, int> EXPIRE(std::string_view key, int seconds) {
        static std::string cmd = GetCmd("EXPIRE", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", seconds));
    }

    template <typename... Args>
    tl::expected<int, int> HDEL(std::string_view key, Args &&...args) {
        static_assert(sizeof...(Args) > 0, "invalid number of arguement");

        static std::string cmd = GetCmd("HDEL", sizeof...(args) + 1);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", args)...);
    }

    template <typename... Args>
    tl::expected<int, int> SADD(std::string_view key, Args &&...args) {
        static_assert(sizeof...(Args) > 0, "invalid number of arguement");

        static std::string cmd = GetCmd("SADD", sizeof...(args) + 1);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", args)...);
    }

    template <typename... Args>
    tl::expected<int, int> SREM(std::string_view key, Args &&...args) {
        static_assert(sizeof...(Args) > 0, "invalid number of arguement");

        static std::string cmd = GetCmd("SREM", sizeof...(args) + 1);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", args)...);
    }

    template <typename T>
    tl::expected<int, int> SISMEMBER(std::string_view key, T&& member) {
        static std::string cmd = GetCmd("SISMEMBER", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", member));
    }

    // template <typename T>
    // ?? SMEMBERS(std::string_view key) {
    //     static_assert(is_redis_reply_convertible<REDIS_REPLY_ARRAY, T>::value,
    //         "no function RedisReplyConvert<REDIS_REPLY_ARRAY, T>::Convert can be called.");
    //     static std::string cmd = GetCmd("SMEMBERS", 1);
    //     return ExcuteCommand<int>(cmd, fmt::format(value), std::string_view(key));
    // }


    tl::expected<std::pair<int, std::vector<std::string>>, int> SSCAN(const std::string_view& key, size_t cursor, const std::string_view& match="",size_t count = 0) {
        std::string cmd = fmt::format("SSCAN {} {}", key, cursor);
        if(!match.empty()) cmd.append(fmt::format(" MATCH {}",match));
        if(count > 0) cmd.append(fmt::format(" COUNT {}",count));
        return ExcuteCommand<std::pair<int,std::vector<std::string>>>(cmd);
    }

    /*
    Integer reply: 0 if the hash does not contain the field, or the key does not exist.
    Integer reply: 1 if the hash contains the field.
    */
    tl::expected<int, int> HEXISTS(std::string_view key, std::string_view field) {
        static std::string cmd = GetCmd("HEXISTS", 2);
        return ExcuteCommand<int>(cmd, std::string(key), std::string(field));
    }

    tl::expected<int, int> EXISTS(std::string_view key) {
        static std::string cmd = GetCmd("EXISTS", 1);
        return ExcuteCommand<int>(cmd, std::string(key));
    }

    template<typename T, typename... Args>
    tl::expected<std::string, int> SET(std::string_view key, T&& value, Args&&... args) {
        static std::string cmd = GetCmd("SET", 2 + sizeof...(args));
        return ExcuteCommand<std::string>(cmd, std::string(key), fmt::format("{}", value), fmt::format("{}", args)...);
    }

    template<typename VT>
    tl::expected<int,int> SETNX(const std::string_view& key,VT&& value){
        static std::string cmd = GetCmd("SETNX", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", value));
    }

    template<typename T>
    tl::expected<T, int> GET(std::string_view key) {
        static std::string cmd = GetCmd("GET", 1);
        return ExcuteCommand<T>(cmd, std::string(key));
    }


    // Integer reply: the value of the field after the increment operation.
    tl::expected<int64_t, int> INCRBY(const std::string_view key, int64_t inc) {
        static std::string cmd = GetCmd("INCRBY", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", inc));
    }

    template<typename ...Args>
    tl::expected<int, int> DEL(Args&& ...keys) {
        static_assert(sizeof...(Args) > 0, "invalid number of arguement");
        static std::string cmd = GetCmd("DEL", sizeof...(keys));
        return ExcuteCommand<int>(cmd, fmt::format("{}", keys)...);
    }

    /*
    Integer reply: TTL in seconds.
    Integer reply: -1 if the key exists but has no associated expiration.
    Integer reply: -2 if the key does not exist.
    */
    tl::expected<int, int> TTL(std::string_view key) {
        static std::string cmd = GetCmd("TTL", 1);
        return ExcuteCommand<int>(cmd, std::string(key));
    }

    // key score1 member1 score2 member2 ...
    template<typename ...Args>
    tl::expected<int, int> ZADD(std::string_view key, Args&& ...keys) {
        static_assert((sizeof...(Args) % 2) == 0 && (sizeof...(Args) > 0),
            "invalid number of arguement");
        static std::string cmd = GetCmd("ZADD", sizeof...(keys) + 1);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", keys)...);
    }

    template<typename ...Args>
    tl::expected<int, int> ZREM(Args&& ...keys) {
        static_assert((sizeof...(Args) > 0), "invalid number of arguement");
        static std::string cmd = GetCmd("ZREM", sizeof...(keys));
        return ExcuteCommand<int>(cmd, fmt::format("{}", keys)...);
    }

    tl::expected<int, int> ZCARD(std::string_view key) {
        static std::string cmd = GetCmd("ZCARD {}", 1);
        return ExcuteCommand<int>(cmd, std::string(key));
    }

    template<typename T>
    tl::expected<int, int> ZSCORE(std::string_view key, T member) {
        static std::string cmd = GetCmd("ZSCORE", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", member));
    }

    template<typename T>
    tl::expected<int, int> ZINCRBY(std::string_view key, int increment, T&& member) {
        static std::string cmd = GetCmd("ZINCRBY", 3);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", increment), fmt::format("{}", member));
    }

    // query WITHSCORES
    tl::expected<std::vector<std::pair<std::string, int>>, int> ZREVRANGE(std::string_view key, int start, int stop) {
        static std::string cmd = GetCmd("ZREVRANGE", 4);
        return ExcuteCommand<std::vector<std::pair<std::string, int>>>(
            cmd, std::string(key), fmt::format("{}", start), fmt::format("{}", stop), std::string("WITHSCORES"));
    }

    /*
    // query WITHSCORES, pair<rank, socre>
    template<typename T>
    tl::expected<std::pair<int, int>, int> ZREVRANK(std::string_view key, T member) {
        std::string cmd = fmt::format("ZREVRANK {} {} WITHSCORE", key, member);
        return ExcuteCommand<std::pair<int, int>>(cmd);
    }
    */

    template<typename T>
    tl::expected<int, int> ZREVRANK(std::string_view key, T member) {
        static std::string cmd = GetCmd("ZREVRANK", 2);
        return ExcuteCommand<int>(cmd, std::string(key), fmt::format("{}", member));
    }

	tl::expected<std::string, int> TryLock(std::string_view lock_key, int px = 3000) {
		return SET(lock_key, 1, RedisOp::PX{ px }, RedisOp::NX{});
    }

	tl::expected<int, int> UnLock(std::string_view lock_key) {
		return DEL(lock_key);
	}

    template <typename T, typename... Args, std::enable_if_t<(std::is_same_v<Args, std::string> && ...), int> = 0>
    tl::expected<T, int> ExcuteCommand(std::string_view command, Args&&... args) {
        redisContext* context = redis_cxt_pool_[0];

        time_t start = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        RedisReply reply = redisCommand(context, command.data(), (args.c_str())...);
        time_t end = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (end - start > 100) {
            LOG_WARN("slow redis: time[%d] command[%s]", end - start, command.data());
        }
        if (!reply) {
            LOG_ERROR("cmd[%s] reply is null, context error[%d:%s]", command.data(),
                context->err, context->errstr);
            //auto_ctx.SetContextDisable();
            return tl::unexpected{ -1 };
        }

        auto _ = GetFromReply<T>(reply);
        if (!_ && _.error() == REDIS_REPLY_ERROR) {
            LOG_ERROR("%s: command[%s]", __FUNCTION__, command.data());
        }
        return _;
    }


protected:
    std::string GetCmd(std::string_view cmd, size_t argc) {
        std::string ret { cmd };
        for (int i = 0; i < argc; i++)
            ret += " %s";

        return std::move(ret);
    }
    int GetResultFromReply(const redisReply* reply, std::string& res);

protected:

    std::vector<redisContext*> redis_cxt_pool_;
};

} // namespace rdsfmt

template<const char* OP, typename T>
struct fmt::formatter<rdsfmt::RedisOp::RedisOptions<OP, T>> : public fmt::formatter<std::string> {
    auto format(const rdsfmt::RedisOp::RedisOptions<OP, T>& op, fmt::format_context& ctx) const {
		auto str = fmt::format("{} {}", op.option, op.value.value());
		return fmt::formatter<std::string>::format(str, ctx);
    }
};

template<const char* OP>
struct fmt::formatter<rdsfmt::RedisOp::RedisOptions<OP, void>> : public fmt::formatter<std::string> {
    auto format(const rdsfmt::RedisOp::RedisOptions<OP, void>& op, fmt::format_context& ctx) const {
        auto str = fmt::format("{}", op.option);
        return fmt::formatter<std::string>::format(str, ctx);
    }
};


#endif // !__REDISFMT_H__

