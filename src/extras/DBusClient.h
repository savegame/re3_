#pragma once

#include <string>
#include <functional>
#include <map>
#include <vector>
#include <memory>
#include <dbus/dbus.h>

// Forward declarations
class DBusClient;

// Variant type for D-Bus arguments
class DBusVariant
{
public:
    enum Type {
        TypeInvalid,
        TypeBool,
        TypeInt32,
        TypeUInt32,
        TypeInt64,
        TypeUInt64,
        TypeDouble,
        TypeString,
        TypeObjectPath,
        TypeArray
    };

    DBusVariant() : m_type(TypeInvalid) {}
    DBusVariant(bool v) : m_type(TypeBool) { m_data.boolVal = v; }
    DBusVariant(int32_t v) : m_type(TypeInt32) { m_data.int32Val = v; }
    DBusVariant(uint32_t v) : m_type(TypeUInt32) { m_data.uint32Val = v; }
    DBusVariant(int64_t v) : m_type(TypeInt64) { m_data.int64Val = v; }
    DBusVariant(uint64_t v) : m_type(TypeUInt64) { m_data.uint64Val = v; }
    DBusVariant(double v) : m_type(TypeDouble) { m_data.doubleVal = v; }
    DBusVariant(const char *v) : m_type(TypeString), m_stringVal(v) {}
    DBusVariant(const std::string &v) : m_type(TypeString), m_stringVal(v) {}

    static DBusVariant ObjectPath(const std::string &path) {
        DBusVariant v;
        v.m_type = TypeObjectPath;
        v.m_stringVal = path;
        return v;
    }

    static DBusVariant Array(const std::vector<DBusVariant> &arr) {
        DBusVariant v;
        v.m_type = TypeArray;
        v.m_arrayVal = arr;
        return v;
    }

    Type GetType() const { return m_type; }
    
    bool IsValid() const { return m_type != TypeInvalid; }
    bool GetBool() const { return m_data.boolVal; }
    int32_t GetInt32() const { return m_data.int32Val; }
    uint32_t GetUInt32() const { return m_data.uint32Val; }
    int64_t GetInt64() const { return m_data.int64Val; }
    uint64_t GetUInt64() const { return m_data.uint64Val; }
    double GetDouble() const { return m_data.doubleVal; }
    const std::string& GetString() const { return m_stringVal; }
    const std::string& GetObjectPath() const { return m_stringVal; }
    const std::vector<DBusVariant>& GetArray() const { return m_arrayVal; }

private:
    Type m_type;
    union {
        bool boolVal;
        int32_t int32Val;
        uint32_t uint32Val;
        int64_t int64Val;
        uint64_t uint64Val;
        double doubleVal;
    } m_data = {};
    std::string m_stringVal;
    std::vector<DBusVariant> m_arrayVal;
};

// Result of a D-Bus call
class DBusResult
{
public:
    DBusResult() : m_success(false) {}
    DBusResult(bool success, const std::string &error = "") 
        : m_success(success), m_error(error) {}

    bool IsSuccess() const { return m_success; }
    const std::string& GetError() const { return m_error; }
    
    const std::vector<DBusVariant>& GetValues() const { return m_values; }
    const DBusVariant& GetValue(size_t index = 0) const {
        static DBusVariant invalid;
        return index < m_values.size() ? m_values[index] : invalid;
    }

    void AddValue(const DBusVariant &v) { m_values.push_back(v); }

private:
    bool m_success;
    std::string m_error;
    std::vector<DBusVariant> m_values;
};

// Callback types
using DBusAsyncCallback = std::function<void(const DBusResult&)>;
using DBusSignalCallback = std::function<void(const std::string& sender, 
                                               const std::vector<DBusVariant>& args)>;
using DBusTimerCallback = std::function<void()>;

// Timer handle
struct DBusTimer
{
    uint32_t id;
    uint32_t intervalMs;
    uint64_t nextFireTime;
    DBusTimerCallback callback;
    bool repeat;
};

// Signal subscription
struct DBusSignalSubscription
{
    uint32_t id;
    std::string interface;
    std::string member;
    std::string path;
    DBusSignalCallback callback;
};

// Main D-Bus client class
class DBusClient
{
public:
    enum BusType {
        SystemBus,
        SessionBus
    };

    DBusClient();
    ~DBusClient();

    // Connection management
    bool Connect(BusType type = SessionBus);
    void Disconnect();
    bool IsConnected() const;

    // Synchronous method call
    DBusResult Call(const std::string &destination,
                    const std::string &path,
                    const std::string &interface,
                    const std::string &method,
                    const std::vector<DBusVariant> &args = {},
                    int timeoutMs = -1);

    // Asynchronous method call
    bool CallAsync(const std::string &destination,
                   const std::string &path,
                   const std::string &interface,
                   const std::string &method,
                   const std::vector<DBusVariant> &args,
                   DBusAsyncCallback callback,
                   int timeoutMs = -1);

    // Signal subscription
    uint32_t SubscribeSignal(const std::string &interface,
                             const std::string &member,
                             const std::string &path,
                             DBusSignalCallback callback);
    void UnsubscribeSignal(uint32_t subscriptionId);

    // Timers
    uint32_t AddTimer(uint32_t intervalMs, DBusTimerCallback callback, bool repeat = true);
    void RemoveTimer(uint32_t timerId);

    // Must be called periodically (e.g., every frame)
    void Process();

    // Get current time in milliseconds
    static uint64_t GetTimeMs();

private:
    DBusConnection *m_connection;
    BusType m_busType;
    
    uint32_t m_nextSubscriptionId;
    uint32_t m_nextTimerId;
    uint32_t m_nextPendingId;

    std::map<uint32_t, DBusSignalSubscription> m_subscriptions;
    std::map<uint32_t, DBusTimer> m_timers;

    struct PendingCall {
        DBusPendingCall *pending;
        DBusAsyncCallback callback;
    };
    std::map<uint32_t, PendingCall> m_pendingCalls;

    static void PendingCallNotify(DBusPendingCall *pending, void *userData);
    void HandlePendingCallComplete(uint32_t id, DBusPendingCall *pending);
    
    bool AppendArgsToMessage(DBusMessage *msg, const std::vector<DBusVariant> &args);
    bool AppendVariantToIter(DBusMessageIter *iter, const DBusVariant &var);
    DBusVariant ReadVariantFromIter(DBusMessageIter *iter);
    void ReadAllArgsFromMessage(DBusMessage *msg, std::vector<DBusVariant> &outArgs);
    
    void ProcessSignal(DBusMessage *msg);
    void ProcessTimers();
};