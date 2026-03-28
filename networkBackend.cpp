#include "networkBackend.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "NetworkSession.h"

using namespace std;

using SessionsT = unordered_map<uint32_t, unique_ptr<NetworkSession>>;
using TerminatingSessionsT = unordered_set<unique_ptr<NetworkSession>>;

namespace {
    void defaultResultCb(uint32_t, const uint8_t*, size_t, void*) {}

    net_RpcResultCb resultCb = defaultResultCb;
    void* resultCbContext = nullptr;

    uint32_t nextSessionId;

    SessionsT sessions;
    TerminatingSessionsT terminatingSessions;

    mutex apiMutex;

    void cleanupTerminatedSessions() {
        auto it = terminatingSessions.begin();
        while (it != terminatingSessions.end()) {
            auto current = it++;
            if ((*current)->HasTerminated()) terminatingSessions.erase(current);
        }
    }

    void closeSession(const SessionsT::iterator& it) {
        it->second->Terminate();

        terminatingSessions.emplace(std::move(it->second));
        sessions.erase(it);

        cleanupTerminatedSessions();
    }
}  // namespace

void net_setRpcCallback(net_RpcResultCb resultCb, void* context) {
    ::resultCb = resultCb;
    resultCbContext = context;
}

uint32_t net_openSession() {
    lock_guard<mutex> lock(apiMutex);

    cleanupTerminatedSessions();

    const uint32_t sessionId = nextSessionId++;
    NetworkSession::RpcResultCb resultCb = [sessionId](const uint8_t* data, size_t len) {
        ::resultCb(sessionId, data, len, resultCbContext);
    };

    sessions.emplace(sessionId, make_unique<NetworkSession>(resultCb));
    sessions[sessionId]->Start();

    return sessionId;
}

void net_closeSession(uint32_t sessionId) {
    lock_guard<mutex> lock(apiMutex);

    const auto it = sessions.find(sessionId);
    if (sessions.find(sessionId) == sessions.end()) {
        cerr << "unable to close session: invalid session ID " << sessionId << endl;
        return;
    }

    closeSession(it);
}

void net_closeAllSessions() {
    lock_guard<mutex> lock(apiMutex);

    auto it = sessions.begin();

    while (it != sessions.end()) {
        auto current = it++;
        closeSession(current);
    }
}

bool net_dispatchRpc(uint32_t sessionId, const uint8_t* data, size_t len) {
    lock_guard<mutex> lock(apiMutex);

    cleanupTerminatedSessions();
    const auto it = sessions.find(sessionId);
    if (sessions.find(sessionId) == sessions.end()) {
        cerr << "unable to dispatch RPC message: invalid session ID " << sessionId << endl;
        return false;
    }

    return it->second->DispatchRpc(data, len);
}
