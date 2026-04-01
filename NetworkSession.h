#ifndef _NETWORK_SESSION_H_
#define _NETWORK_SESSION_H_

#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET sock_t;
    constexpr sock_t INVALID_SOCK = INVALID_SOCKET;
#else
    typedef int sock_t;
    constexpr sock_t INVALID_SOCK = -1;
#endif

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

struct DnsSettings {
    uint32_t primary;
    uint32_t secondary;
};

class NetworkSession {
   public:
    using RpcResultCb = std::function<void(const uint8_t* data, size_t len)>;

   public:
    explicit NetworkSession(RpcResultCb resultCb);

    void Start();
    void Terminate();

    bool DispatchRpc(const uint8_t* data, size_t len);

    void SetDnsServers(uint32_t primary, uint32_t secondary);

    bool HasTerminated();

   private:
    struct SocketContext {
        explicit SocketContext(sock_t sock);

        sock_t sock{INVALID_SOCK};
        bool blocking{true};
    };

    struct Buffer {
        size_t size{0};
        std::unique_ptr<uint8_t[]> data;
    };

   private:
    void WorkerMain();

    void HandleRpcRequest(MsgRequest& request, const Buffer& payloadBuffer);
    void SendResponse(MsgResponse& response, size_t size);

    void HandleSocketOpen(MsgSocketOpenRequest& request, MsgResponse& response);
    void HandleSocketClose(MsgSocketCloseRequest& request, MsgResponse& response);
    void HandleSocketOptionSet(MsgSocketOptionSetRequest& request, MsgResponse& response);
    void HandleSocketOptionGet(MsgSocketOptionGetRequest& request, MsgResponse& respose);
    void HandleSocketAddr(MsgSocketAddrRequest& request, MsgResponse& response);
    void HandleSocketBind(MsgSocketBindRequest& request, MsgResponse& response);
    void HandleSocketConnect(MsgSocketConnectRequest& request, MsgResponse& response);
    void HandleSelect(MsgSelectRequest& request, MsgResponse& response);
    void HandleSocketSend(MsgSocketSendRequest& request, const Buffer& sendPayload,
                          MsgResponse& response);
    void HandleSocketReceive(MsgSocketReceiveRequest& request, Buffer* receivePayload,
                             MsgResponse& response);
    void HandleSettingsGet(MsgSettingGetRequest& request, MsgResponse& response);
    void HandleSettingsGetDNS(MsgSettingGetResponse& resp, size_t dnsLevel);
    void HandleGetHostByName(MsgGetHostByNameRequest& request, MsgResponse& response);
    void HandleGetServByName(MsgGetServByNameRequest& request, MsgResponse& response);
    void HandleSocketShutdown(MsgSocketShutdownRequest& request, MsgResponse& response);
    void HandleSocketListen(MsgSocketListenRequest& request, MsgResponse& response);
    void HandleSocketAccept(MsgSocketAcceptRequest& request, MsgResponse& response);

    int32_t GetFreeHandle();
    sock_t SocketForHandle(uint32_t handle) const;
    std::optional<uint32_t> HandleForSocket(sock_t sock) const;

    static bool bufferEncodeCb(pb_ostream_t* stream, const pb_field_iter_t* field,
                               void* const* arg);
    static bool bufferDecodeCb(pb_istream_t* stream, const pb_field_iter_t* field, void** arg);
    static bool payloadDecodeCb(pb_istream_t* stream, const pb_field_iter_t* field, void** arg);

   private:
    static constexpr size_t MAX_HANDLE = 32;

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

    std::optional<DnsSettings> dnsSettings{std::nullopt};
    std::mutex dnsSettingsMutex;

   private:
    NetworkSession(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession& operator=(NetworkSession&&) = delete;
};

#endif  // _NETWORK_SESSION_H_