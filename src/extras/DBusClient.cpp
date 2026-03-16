#include "DBusClient.h"
#include <cstdio>
#include <cstring>
#include <sys/time.h>

DBusClient::DBusClient()
    : m_connection(nullptr)
    , m_busType(SessionBus)
    , m_nextSubscriptionId(1)
    , m_nextTimerId(1)
    , m_nextPendingId(1)
{
}

DBusClient::~DBusClient()
{
    Disconnect();
}

uint64_t DBusClient::GetTimeMs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool DBusClient::Connect(BusType type)
{
    if (m_connection) {
        Disconnect();
    }

    m_busType = type;
    DBusError error;
    dbus_error_init(&error);

    DBusBusType dbusType = (type == SystemBus) ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION;
    m_connection = dbus_bus_get(dbusType, &error);

    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "DBusClient: Connection error: %s\n", error.message);
        dbus_error_free(&error);
        return false;
    }

    if (!m_connection) {
        fprintf(stderr, "DBusClient: Failed to connect\n");
        return false;
    }

    // Don't exit on disconnect
    dbus_connection_set_exit_on_disconnect(m_connection, FALSE);

    fprintf(stderr, "DBusClient: Connected to %s bus\n", 
            type == SystemBus ? "system" : "session");
    return true;
}

void DBusClient::Disconnect()
{
    // Cancel pending calls
    for (auto &pair : m_pendingCalls) {
        if (pair.second.pending) {
            dbus_pending_call_cancel(pair.second.pending);
            dbus_pending_call_unref(pair.second.pending);
        }
    }
    m_pendingCalls.clear();

    m_subscriptions.clear();
    m_timers.clear();

    if (m_connection) {
        dbus_connection_unref(m_connection);
        m_connection = nullptr;
    }
}

bool DBusClient::IsConnected() const
{
    return m_connection && dbus_connection_get_is_connected(m_connection);
}

bool DBusClient::AppendVariantToIter(DBusMessageIter *iter, const DBusVariant &var)
{
    switch (var.GetType()) {
        case DBusVariant::TypeBool: {
            dbus_bool_t v = var.GetBool() ? TRUE : FALSE;
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &v);
        }
        case DBusVariant::TypeInt32: {
            int32_t v = var.GetInt32();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &v);
        }
        case DBusVariant::TypeUInt32: {
            uint32_t v = var.GetUInt32();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &v);
        }
        case DBusVariant::TypeInt64: {
            int64_t v = var.GetInt64();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_INT64, &v);
        }
        case DBusVariant::TypeUInt64: {
            uint64_t v = var.GetUInt64();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT64, &v);
        }
        case DBusVariant::TypeDouble: {
            double v = var.GetDouble();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &v);
        }
        case DBusVariant::TypeString: {
            const char *v = var.GetString().c_str();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &v);
        }
        case DBusVariant::TypeObjectPath: {
            const char *v = var.GetObjectPath().c_str();
            return dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &v);
        }
        default:
            return false;
    }
}

bool DBusClient::AppendArgsToMessage(DBusMessage *msg, const std::vector<DBusVariant> &args)
{
    if (args.empty()) return true;

    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    for (const auto &arg : args) {
        if (!AppendVariantToIter(&iter, arg)) {
            return false;
        }
    }
    return true;
}

DBusVariant DBusClient::ReadVariantFromIter(DBusMessageIter *iter)
{
    int type = dbus_message_iter_get_arg_type(iter);

    switch (type) {
        case DBUS_TYPE_BOOLEAN: {
            dbus_bool_t v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v != FALSE);
        }
        case DBUS_TYPE_INT32: {
            int32_t v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v);
        }
        case DBUS_TYPE_UINT32: {
            uint32_t v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v);
        }
        case DBUS_TYPE_INT64: {
            int64_t v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v);
        }
        case DBUS_TYPE_UINT64: {
            uint64_t v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v);
        }
        case DBUS_TYPE_DOUBLE: {
            double v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v);
        }
        case DBUS_TYPE_STRING: {
            const char *v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant(v ? v : "");
        }
        case DBUS_TYPE_OBJECT_PATH: {
            const char *v;
            dbus_message_iter_get_basic(iter, &v);
            return DBusVariant::ObjectPath(v ? v : "");
        }
        case DBUS_TYPE_ARRAY: {
            std::vector<DBusVariant> arr;
            DBusMessageIter subIter;
            dbus_message_iter_recurse(iter, &subIter);
            while (dbus_message_iter_get_arg_type(&subIter) != DBUS_TYPE_INVALID) {
                arr.push_back(ReadVariantFromIter(&subIter));
                dbus_message_iter_next(&subIter);
            }
            return DBusVariant::Array(arr);
        }
        case DBUS_TYPE_VARIANT: {
            DBusMessageIter subIter;
            dbus_message_iter_recurse(iter, &subIter);
            return ReadVariantFromIter(&subIter);
        }
        default:
            return DBusVariant();
    }
}

void DBusClient::ReadAllArgsFromMessage(DBusMessage *msg, std::vector<DBusVariant> &outArgs)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter)) {
        return;
    }

    do {
        outArgs.push_back(ReadVariantFromIter(&iter));
    } while (dbus_message_iter_next(&iter));
}

DBusResult DBusClient::Call(const std::string &destination,
                            const std::string &path,
                            const std::string &interface,
                            const std::string &method,
                            const std::vector<DBusVariant> &args,
                            int timeoutMs)
{
    if (!IsConnected()) {
        return DBusResult(false, "Not connected");
    }

    DBusMessage *msg = dbus_message_new_method_call(
        destination.c_str(),
        path.c_str(),
        interface.c_str(),
        method.c_str()
    );

    if (!msg) {
        return DBusResult(false, "Failed to create message");
    }

    if (!AppendArgsToMessage(msg, args)) {
        dbus_message_unref(msg);
        return DBusResult(false, "Failed to append arguments");
    }

    DBusError error;
    dbus_error_init(&error);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        m_connection, msg, timeoutMs, &error
    );

    dbus_message_unref(msg);

    if (dbus_error_is_set(&error)) {
        std::string errMsg = error.message ? error.message : "Unknown error";
        dbus_error_free(&error);
        return DBusResult(false, errMsg);
    }

    if (!reply) {
        return DBusResult(false, "No reply received");
    }

    DBusResult result(true);
    ReadAllArgsFromMessage(reply, const_cast<std::vector<DBusVariant>&>(result.GetValues()));
    dbus_message_unref(reply);

    return result;
}

void DBusClient::PendingCallNotify(DBusPendingCall *pending, void *userData)
{
    // userData contains packed client pointer and call ID
    uintptr_t packed = reinterpret_cast<uintptr_t>(userData);
    uint32_t callId = static_cast<uint32_t>(packed & 0xFFFFFFFF);
    DBusClient *client = reinterpret_cast<DBusClient*>(packed & ~0xFFFFFFFFULL);
    
    // This approach is fragile, use a different method
    // For simplicity, we'll process in Process() loop instead
}

bool DBusClient::CallAsync(const std::string &destination,
                           const std::string &path,
                           const std::string &interface,
                           const std::string &method,
                           const std::vector<DBusVariant> &args,
                           DBusAsyncCallback callback,
                           int timeoutMs)
{
    if (!IsConnected()) {
        if (callback) {
            callback(DBusResult(false, "Not connected"));
        }
        return false;
    }

    DBusMessage *msg = dbus_message_new_method_call(
        destination.c_str(),
        path.c_str(),
        interface.c_str(),
        method.c_str()
    );

    if (!msg) {
        if (callback) {
            callback(DBusResult(false, "Failed to create message"));
        }
        return false;
    }

    if (!AppendArgsToMessage(msg, args)) {
        dbus_message_unref(msg);
        if (callback) {
            callback(DBusResult(false, "Failed to append arguments"));
        }
        return false;
    }

    DBusPendingCall *pending = nullptr;
    if (!dbus_connection_send_with_reply(m_connection, msg, &pending, timeoutMs)) {
        dbus_message_unref(msg);
        if (callback) {
            callback(DBusResult(false, "Failed to send message"));
        }
        return false;
    }

    dbus_message_unref(msg);

    if (!pending) {
        if (callback) {
            callback(DBusResult(false, "No pending call created"));
        }
        return false;
    }

    uint32_t callId = m_nextPendingId++;
    m_pendingCalls[callId] = { pending, callback };

    return true;
}

void DBusClient::HandlePendingCallComplete(uint32_t id, DBusPendingCall *pending)
{
    auto it = m_pendingCalls.find(id);
    if (it == m_pendingCalls.end()) return;

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    DBusAsyncCallback callback = it->second.callback;

    dbus_pending_call_unref(pending);
    m_pendingCalls.erase(it);

    if (!reply) {
        if (callback) {
            callback(DBusResult(false, "No reply"));
        }
        return;
    }

    int type = dbus_message_get_type(reply);
    if (type == DBUS_MESSAGE_TYPE_ERROR) {
        const char *errName = dbus_message_get_error_name(reply);
        dbus_message_unref(reply);
        if (callback) {
            callback(DBusResult(false, errName ? errName : "Error"));
        }
        return;
    }

    DBusResult result(true);
    ReadAllArgsFromMessage(reply, const_cast<std::vector<DBusVariant>&>(result.GetValues()));
    dbus_message_unref(reply);

    if (callback) {
        callback(result);
    }
}

uint32_t DBusClient::SubscribeSignal(const std::string &interface,
                                     const std::string &member,
                                     const std::string &path,
                                     DBusSignalCallback callback)
{
    if (!IsConnected()) return 0;

    // Build match rule
    std::string rule = "type='signal'";
    if (!interface.empty()) {
        rule += ",interface='" + interface + "'";
    }
    if (!member.empty()) {
        rule += ",member='" + member + "'";
    }
    if (!path.empty()) {
        rule += ",path='" + path + "'";
    }

    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(m_connection, rule.c_str(), &error);

    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "DBusClient: Failed to add match: %s\n", error.message);
        dbus_error_free(&error);
        return 0;
    }

    dbus_connection_flush(m_connection);

    uint32_t id = m_nextSubscriptionId++;
    m_subscriptions[id] = { id, interface, member, path, callback };

    return id;
}

void DBusClient::UnsubscribeSignal(uint32_t subscriptionId)
{
    auto it = m_subscriptions.find(subscriptionId);
    if (it == m_subscriptions.end()) return;

    if (IsConnected()) {
        std::string rule = "type='signal'";
        if (!it->second.interface.empty()) {
            rule += ",interface='" + it->second.interface + "'";
        }
        if (!it->second.member.empty()) {
            rule += ",member='" + it->second.member + "'";
        }
        if (!it->second.path.empty()) {
            rule += ",path='" + it->second.path + "'";
        }

        DBusError error;
        dbus_error_init(&error);
        dbus_bus_remove_match(m_connection, rule.c_str(), &error);
        dbus_error_free(&error);
    }

    m_subscriptions.erase(it);
}

uint32_t DBusClient::AddTimer(uint32_t intervalMs, DBusTimerCallback callback, bool repeat)
{
    uint32_t id = m_nextTimerId++;
    
    DBusTimer timer;
    timer.id = id;
    timer.intervalMs = intervalMs;
    timer.nextFireTime = GetTimeMs() + intervalMs;
    timer.callback = callback;
    timer.repeat = repeat;
    
    m_timers[id] = timer;
    return id;
}

void DBusClient::RemoveTimer(uint32_t timerId)
{
    m_timers.erase(timerId);
}

void DBusClient::ProcessSignal(DBusMessage *msg)
{
    const char *interface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path = dbus_message_get_path(msg);
    const char *sender = dbus_message_get_sender(msg);

    std::vector<DBusVariant> args;
    ReadAllArgsFromMessage(msg, args);

    for (auto &pair : m_subscriptions) {
        DBusSignalSubscription &sub = pair.second;

        bool match = true;
        if (!sub.interface.empty() && (!interface || sub.interface != interface)) {
            match = false;
        }
        if (!sub.member.empty() && (!member || sub.member != member)) {
            match = false;
        }
        if (!sub.path.empty() && (!path || sub.path != path)) {
            match = false;
        }

        if (match && sub.callback) {
            sub.callback(sender ? sender : "", args);
        }
    }
}

void DBusClient::ProcessTimers()
{
    uint64_t now = GetTimeMs();
    std::vector<uint32_t> toRemove;

    for (auto &pair : m_timers) {
        DBusTimer &timer = pair.second;
        
        if (now >= timer.nextFireTime) {
            if (timer.callback) {
                timer.callback();
            }
            
            if (timer.repeat) {
                timer.nextFireTime = now + timer.intervalMs;
            } else {
                toRemove.push_back(timer.id);
            }
        }
    }

    for (uint32_t id : toRemove) {
        m_timers.erase(id);
    }
}

void DBusClient::Process()
{
    if (!IsConnected()) return;

    // Process pending async calls
    std::vector<uint32_t> completedCalls;
    for (auto &pair : m_pendingCalls) {
        if (dbus_pending_call_get_completed(pair.second.pending)) {
            completedCalls.push_back(pair.first);
        }
    }
    for (uint32_t id : completedCalls) {
        auto it = m_pendingCalls.find(id);
        if (it != m_pendingCalls.end()) {
            HandlePendingCallComplete(id, it->second.pending);
        }
    }

    // Process incoming messages
    while (dbus_connection_get_dispatch_status(m_connection) == DBUS_DISPATCH_DATA_REMAINS) {
        dbus_connection_dispatch(m_connection);
    }

    // Non-blocking read
    dbus_connection_read_write(m_connection, 0);

    while (true) {
        DBusMessage *msg = dbus_connection_pop_message(m_connection);
        if (!msg) break;

        int type = dbus_message_get_type(msg);
        if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
            ProcessSignal(msg);
        }

        dbus_message_unref(msg);
    }

    // Process timers
    ProcessTimers();
}