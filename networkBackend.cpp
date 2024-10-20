#include "networkBackend.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "NetworkSession.h"

using namespace std;

namespace {
    void defaultResultCb(unsigned int sessiondId, void* data, size_t len, void* context) {}

    net_RpcResultCb resultCb = defaultResultCb;
    void* resultCbContext = nullptr;

    uint32_t nextSessionId;

    unordered_map<uint32_t, unique_ptr<NetworkSession>> sessions;
    unordered_set<unique_ptr<NetworkSession>> terminatingSessions;

    void cleanupTerminatedSessions() {
        for (auto it = terminatingSessions.begin(); it != terminatingSessions.end(); it++)
            if ((*it)->HasTerminated()) terminatingSessions.erase(it);
    }
}  // namespace

void net_setRpcCallback(net_RpcResultCb resultCb, void* context) {
    ::resultCb = resultCb;
    resultCbContext = context;
}

unsigned int net_openSession() {
    cleanupTerminatedSessions();

    const uint32_t sessionId = nextSessionId++;
    NetworkSession::RpcResultCb resultCb = [sessionId](void* data, size_t len) {
        ::resultCb(sessionId, data, len, resultCbContext);
    };

    sessions.emplace(sessionId, make_unique<NetworkSession>(resultCb));
    sessions[sessionId]->Start();

    return sessionId;
}

void net_closeSession(unsigned int sessionId) {
    const auto it = sessions.find(sessionId);
    if (sessions.find(sessionId) == sessions.end()) {
        cerr << "unable to close session: invalid session ID " << sessionId << endl;
        return;
    }

    it->second->Terminate();

    terminatingSessions.emplace(std::move(it->second));
    sessions.erase(it);

    cleanupTerminatedSessions();
}

bool net_dispatchRpc(unsigned int sessionId, void* data, size_t len) {
    cleanupTerminatedSessions();

    const auto it = sessions.find(sessionId);
    if (sessions.find(sessionId) == sessions.end()) {
        cerr << "unable to dispatch RPC message: invalid session ID " << sessionId << endl;
        return false;
    }

    return it->second->DispatchRpc(data, len);
}
