#include "NetworkSession.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "codes.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "sockopt.h"

using namespace std;

#define LOGGING

#ifdef LOGGING
    #define LOG(...) fprintf(stderr, __VA_ARGS__);
#else
    #define LOG(...)
#endif

namespace {
    constexpr size_t INITIAL_SIZE_REQUEST = 1024;
    constexpr size_t INITIAL_SIZE_RESPONSE = 1024;

    constexpr size_t RESPONSE_STATIC_SIZE = 128;

    template <typename F, typename... Ts>
    int withRetry(F fn, Ts... args) {
        int result;
        do {
            result = fn(args...);
        } while (result == -1 && errno == EINTR);

        return result;
    }

    int mapSocketType(uint32_t palmType) {
        switch (palmType) {
            case 1:
                return SOCK_STREAM;

            case 2:
                return SOCK_DGRAM;

            case 3:
                return SOCK_RAW;

            default:
                return -1;
        }
    }

    bool encodeSockaddr(const sockaddr* saddr, Address& addr, socklen_t len) {
        if (saddr->sa_family != AF_INET || len < sizeof(sockaddr_in)) return false;
        const auto saddr4 = reinterpret_cast<const sockaddr_in*>(saddr);

        addr.ip = ntohl(saddr4->sin_addr.s_addr);
        addr.port = ntohs(saddr4->sin_port);

        return true;
    }
}  // namespace

NetworkSession::NetworkSession(RpcResultCb resultCb)
    : resultCb(resultCb), rpcRequest(INITIAL_SIZE_REQUEST), rpcResponse(INITIAL_SIZE_RESPONSE) {}

void NetworkSession::Start() {
    if (hasStarted) return;

    worker = thread(bind(&NetworkSession::WorkerMain, this));
    hasStarted = true;

    LOG("network session started\n");
}

void NetworkSession::Terminate() {
    unique_lock<mutex> lock(dispatchMutex);

    terminateRequested = true;
    dispatchCv.notify_one();

    LOG("network session closed\n");
}

bool NetworkSession::DispatchRpc(const uint8_t* data, size_t len) {
    LOG("dispatch rpc\n");

    unique_lock<mutex> lock(dispatchMutex);

    if (terminateRequested) {
        cerr << "unable to dispatch: session is already terminating" << endl;
        return false;
    }

    if (rpcRequestPending) {
        cerr << "unable to dispatch: pending RPC request" << endl;
        return false;
    }

    if (rpcRequest.size() < len) rpcRequest.resize(len);
    rpcRequestSize = len;
    memcpy(rpcRequest.data(), data, len);

    rpcRequestPending = true;
    dispatchCv.notify_one();

    return true;
}

bool NetworkSession::HasTerminated() { return !hasStarted || hasTerminated; }

void NetworkSession::WorkerMain() {
    while (true) {
        {
            unique_lock<mutex> lock(dispatchMutex);

            rpcRequestPending = false;
            while (!rpcRequestPending && !terminateRequested) dispatchCv.wait(lock);

            if (terminateRequested) break;
        }

        pb_istream_t stream = pb_istream_from_buffer(rpcRequest.data(), rpcRequestSize);
        MsgRequest request;

        if (!pb_decode(&stream, MsgRequest_fields, &request)) {
            cerr << "failed to decode RPC request" << endl;
            request.id = ~0;
        }

        HandleRpcRequest(request);
    }

    worker.detach();

    for (const auto& ctx : sockets) {
        if (!ctx) continue;

        withRetry(shutdown, ctx->sock, SHUT_RDWR);
        withRetry(close, ctx->sock);
    }

    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest(MsgRequest& request) {
    MsgResponse response = MsgResponse_init_zero;

    switch (request.which_payload) {
        case MsgRequest_socketOpenRequest_tag:
            HandleSocketOpen(request.payload.socketOpenRequest, response);
            break;

        case MsgRequest_socketCloseRequest_tag:
            HandleSocketClose(request.payload.socketCloseRequest, response);
            break;

        case MsgRequest_socketOptionSetRequest_tag:
            HandleSocketOptionSet(request.payload.socketOptionSetRequest, response);
            break;

        case MsgRequest_socketAddrRequest_tag:
            HandleSocketAddr(request.payload.socketAddrRequest, response);
            break;

        default:
            response.which_payload = MsgResponse_invalidRequestResponse_tag;
            response.payload.invalidRequestResponse.tag = true;
            break;
    }

    response.id = request.id;

    SendResponse(response, RESPONSE_STATIC_SIZE);
}

void NetworkSession::HandleSocketOpen(MsgSocketOpenRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketOpenResponse_tag;
    auto& resp = response.payload.socketOpenResponse;

    resp.handle = -1;
    resp.err = 0;

    int socketType = mapSocketType(request.type);
    if (socketType < 0) {
        LOG("bad socket type %i\n", request.type);
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (socketType == SOCK_RAW) {
        LOG("TODO: RAW sockets currently unsupported\n");
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    int32_t handle = GetFreeHandle();
    if (handle < 0) {
        LOG("not free handles left");
        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    int fd = withRetry(socket, AF_INET, socketType, 0);
    if (fd == -1) {
        resp.err = NetworkCodes::errnoToPalm(errno);
        return;
    }

    sockets[handle] = {.sock = fd};
    resp.handle = handle;
}

void NetworkSession::HandleSocketClose(MsgSocketCloseRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketCloseResponse_tag;
    auto& resp = response.payload.socketCloseResponse;

    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    sockets[request.handle] = nullopt;

    withRetry(shutdown, sock, SHUT_RDWR);
    if (withRetry(close, sock) == -1) {
        resp.err = NetworkCodes::errnoToPalm(errno);
    }
}

void NetworkSession::HandleSocketOptionSet(MsgSocketOptionSetRequest& request,
                                           MsgResponse& response) {
    response.which_payload = MsgResponse_socketOptionSetResponse_tag;
    auto& resp = response.payload.socketOptionSetResponse;

    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (request.level == NetworkCodes::netSocketOptLevelSocket &&
        request.option == NetworkCodes::netSocketOptSockNonBlocking) {
        if (request.which_value != MsgSocketOptionSetRequest_intval_tag) {
            resp.err = NetworkCodes::netErrParamErr;
            return;
        }

        int flags = withRetry(fcntl, sock, F_GETFL);

        if (request.value.intval)
            flags |= O_NONBLOCK;
        else
            flags &= ~O_NONBLOCK;

        if (withRetry(fcntl, sock, F_SETFL, flags) == -1) {
            resp.err = NetworkCodes::errnoToPalm(errno);
        }

        return;
    }

    NetworkSockopt::SockoptParameters parameters;
    if (!NetworkSockopt::translateSetSockoptParameters(request, parameters)) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (withRetry(setsockopt, sock, parameters.level, parameters.name, &parameters.payload,
                  parameters.len)) {
        resp.err = NetworkCodes::errnoToPalm(errno);
    }
}

void NetworkSession::HandleSocketAddr(MsgSocketAddrRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketAddrResponse_tag;
    auto& resp = response.payload.socketAddrResponse;

    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (request.requestAddressLocal) {
        sockaddr addr;
        socklen_t addrLen = sizeof(addr);

        if (withRetry(getsockname, sock, &addr, &addrLen) == -1) {
            resp.err = NetworkCodes::errnoToPalm(errno);
            return;
        }

        if (!encodeSockaddr(&addr, resp.addressLocal, addrLen)) {
            resp.err = NetworkCodes::netErrInternal;
            return;
        }

        resp.has_addressLocal = true;
    }

    if (request.requestAddressRemote) {
        sockaddr addr;
        socklen_t addrLen = sizeof(addr);

        if (withRetry(getpeername, sock, &addr, &addrLen) == -1) {
            resp.err = NetworkCodes::errnoToPalm(errno);
            return;
        }

        if (!encodeSockaddr(&addr, resp.addressRemote, addrLen)) {
            resp.err = NetworkCodes::netErrInternal;
            return;
        }

        resp.has_addressRemote = true;
    }
}

int32_t NetworkSession::GetFreeHandle() {
    for (size_t i = 0; i < sockets.size(); i++)
        if (!sockets[i]) return i;

    return -1;
}

int NetworkSession::ResolveHandle(uint32_t handle) const {
    if (handle > MAX_HANDLE) return -1;

    const auto& socketContext = sockets[handle];
    if (!socketContext) return -1;

    return socketContext->sock;
}

void NetworkSession::SendResponse(MsgResponse& response, size_t size) {
    if (rpcResponse.size() < size) rpcResponse.resize(size);
    pb_ostream_t stream = pb_ostream_from_buffer(rpcResponse.data(), size);

    pb_encode(&stream, MsgResponse_fields, &response);

    resultCb(rpcResponse.data(), stream.bytes_written);
}
