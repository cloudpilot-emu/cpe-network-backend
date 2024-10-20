#ifndef NETWORK_SESSION_H
#define NETWORK_SESSION_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "networking.pb.h"

class NetworkSession {
   public:
    using RpcResultCb = std::function<void(void* data, size_t len)>;

   public:
    explicit NetworkSession(RpcResultCb resultCb);

    void Start();
    void Terminate();

    bool DispatchRpc(void* data, size_t len);

    bool HasTerminated();

   private:
    void WorkerMain();

    void HandleRpcRequest();
    void SendResponse(MsgResponse& response, size_t size);

   private:
    RpcResultCb resultCb;

    std::vector<uint8_t> rpcRequest;
    bool rpcRequestPending{false};

    std::vector<uint8_t> rpcResponse;

    std::mutex dispatchMutex;
    std::condition_variable dispatchCv;
    std::thread worker;

    bool hasStarted{false};
    bool terminateRequested{false};
    std::atomic_bool hasTerminated{false};

   private:
    NetworkSession(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession& operator=(NetworkSession&&) = delete;
};

#endif  // NETWORK_SESSION_H