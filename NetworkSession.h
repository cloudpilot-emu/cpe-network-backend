#ifndef _NETWORK_SESSION_H_
#define _NETWORK_SESSION_H_

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "networking.pb.h"

class NetworkSession {
   public:
    using RpcResultCb = std::function<void(const uint8_t* data, size_t len)>;

   public:
    explicit NetworkSession(RpcResultCb resultCb);

    void Start();
    void Terminate();

    bool DispatchRpc(const uint8_t* data, size_t len);

    bool HasTerminated();

   private:
    void WorkerMain();

    void HandleRpcRequest(MsgRequest& request);
    void SendResponse(MsgResponse& response, size_t size);

    void HandleSocketOpen(MsgSocketOpenRequest& request, MsgResponse& response);
    void HandleSocketClose(MsgSocketCloseRequest& request, MsgResponse& response);
    void HandleSocketOptionSet(MsgSocketOptionSetRequest& request, MsgResponse& response);

    int32_t GetFreeHandle();
    int ResolveHandle(uint32_t handle) const;

   private:
    struct SocketContext {
        int sock;
    };

   private:
    static constexpr size_t MAX_HANDLE = 31;

    RpcResultCb resultCb;

    std::vector<uint8_t> rpcRequest;
    size_t rpcRequestSize{0};
    bool rpcRequestPending{false};

    std::vector<uint8_t> rpcResponse;

    std::mutex dispatchMutex;
    std::condition_variable dispatchCv;
    std::thread worker;

    bool hasStarted{false};
    bool terminateRequested{false};
    std::atomic_bool hasTerminated{false};

    std::array<std::optional<SocketContext>, MAX_HANDLE + 1> sockets;

   private:
    NetworkSession(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession& operator=(NetworkSession&&) = delete;
};

#endif  // _NETWORK_SESSION_H_