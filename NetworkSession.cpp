#include "NetworkSession.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

#include "codes.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "sockopt.h"

// TODO
//
// * Support ICMP / ICMP over raw socket
// * Test blocking code paths for connect, send

using namespace std;

#define LOGGING

#ifdef LOGGING
    #define LOG(...) fprintf(stderr, __VA_ARGS__);
#else
    #define LOG(...)
#endif

#if defined(BSD) || defined(__BSD__)
    #define HAVE_SIN_LEN
#endif

namespace {
    constexpr size_t INITIAL_SIZE_REQUEST = 1024;
    constexpr size_t INITIAL_SIZE_RESPONSE = 1024;

    constexpr size_t RESPONSE_STATIC_SIZE = 128;

    constexpr uint32_t MAX_TIMEOUT = 10000;

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
        if (len < sizeof(sockaddr_in) || saddr->sa_family != AF_INET) return false;
        const auto saddr4 = reinterpret_cast<const sockaddr_in*>(saddr);

        addr.ip = ntohl(saddr4->sin_addr.s_addr);
        addr.port = ntohs(saddr4->sin_port);

        return true;
    }

    void decodeSockaddr(Address& addr, sockaddr_in& saddr) {
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = htonl(addr.ip);
        saddr.sin_port = htons(addr.port);

#ifdef HAVE_SIN_LEN
        saddr.sin_len = sizeof(sockaddr_in);
#endif
    }

    bool setNonBlocking(int sock) {
        int flags = withRetry(fcntl, sock, F_GETFL);
        if (flags == -1) return false;

        flags |= O_NONBLOCK;

        return withRetry(fcntl, sock, F_SETFL, flags) != -1;
    }

    uint16_t getSocketError(int sock) {
        int err;
        socklen_t len = sizeof(err);

        if (withRetry(getsockopt, sock, SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
            cerr << "unable to retrieve socket error: " << errno << endl;

            return NetworkCodes::netErrInternal;
        }

        return NetworkCodes::errnoToPalm(err);
    }

    uint32_t normalizeTimeout(int timeout) {
        return timeout < 0 ? MAX_TIMEOUT : min(static_cast<uint32_t>(timeout), MAX_TIMEOUT);
    }

    int64_t timestampMsec() {
        return chrono::duration_cast<chrono::milliseconds>(
                   chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    int translateIOFlags(uint16_t flags) {
        int ioflags = 0;

        if (flags & 0x01) ioflags |= MSG_OOB;
        if (flags & 0x02) ioflags |= MSG_PEEK;
        if (flags & 0x04) ioflags |= MSG_DONTROUTE;

        return ioflags;
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

            while (!rpcRequestPending && !terminateRequested) dispatchCv.wait(lock);

            if (terminateRequested) break;
        }

        pb_istream_t stream = pb_istream_from_buffer(rpcRequest.data(), rpcRequestSize);
        MsgRequest request;

        Buffer payloadBuffer;
        request.cb_payload.arg = &payloadBuffer;
        request.cb_payload.funcs.decode = payloadDecodeCb;

        if (!pb_decode(&stream, MsgRequest_fields, &request)) {
            cerr << "failed to decode RPC request" << endl;
            request.id = ~0;
        }

        HandleRpcRequest(request, payloadBuffer);
    }

    worker.detach();

    for (const auto& ctx : sockets) {
        if (!ctx) continue;

        withRetry(shutdown, ctx->sock, SHUT_RDWR);
        withRetry(close, ctx->sock);
    }

    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest(MsgRequest& request, const Buffer& payloadBuffer) {
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

        case MsgRequest_socketBindRequest_tag:
            HandleSocketBind(request.payload.socketBindRequest, response);
            break;

        case MsgRequest_socketConnectRequest_tag:
            HandleSocketConnect(request.payload.socketConnectRequest, response);
            break;

        case MsgRequest_selectRequest_tag:
            HandleSelect(request.payload.selectRequest, response);
            break;

        case MsgRequest_socketSendRequest_tag:
            HandleSocketSend(request.payload.socketSendRequest, payloadBuffer, response);
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

    int sock = withRetry(socket, AF_INET, socketType, 0);
    if (sock == -1) {
        resp.err = NetworkCodes::errnoToPalm(errno);
        return;
    }

    if (!setNonBlocking(sock)) {
        cerr << "failed to set socket non-blocking: " << errno << endl;
        close(sock);

        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    sockets[handle] = SocketContext(sock);
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

        sockets[request.handle]->blocking = !request.value.intval;

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

void NetworkSession::HandleSocketBind(MsgSocketBindRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketBindResponse_tag;
    auto& resp = response.payload.socketBindResponse;

    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    sockaddr_in saddr;
    decodeSockaddr(request.address, saddr);

    if (withRetry(::bind, sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == -1) {
        resp.err = NetworkCodes::errnoToPalm(errno);
    }
}

void NetworkSession::HandleSocketConnect(MsgSocketConnectRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketConnectResponse_tag;
    auto& resp = response.payload.socketConnectResponse;

    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    const SocketContext& ctx = *sockets[request.handle];
    sockaddr_in saddr;
    decodeSockaddr(request.address, saddr);

    if (withRetry(connect, sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0) return;

    if ((errno != EINPROGRESS && errno != EALREADY) || !ctx.blocking) {
        resp.err = NetworkCodes::errnoToPalm(errno);
        return;
    }

    pollfd fds[] = {
        {.fd = sock, .events = POLLERR | POLLHUP | POLLRDNORM | POLLWRNORM, .revents = 0}};

    switch (withRetry(poll, fds, 1, normalizeTimeout(request.timeout))) {
        case -1:
            cerr << "poll failed during connect: " << errno << endl;
            resp.err = NetworkCodes::netErrInternal;
            return;

        case 0:
            resp.err = NetworkCodes::netErrTimeout;
            return;

        default:
            break;
    }

    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        resp.err = getSocketError(sock);
    }
}

void NetworkSession::HandleSelect(MsgSelectRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_selectResponse_tag;
    auto& resp = response.payload.selectResponse;

    resp.err = 0;
    resp.exceptFDs = 0;
    resp.readFDs = 0;
    resp.writeFDs = 0;

    const uint32_t fdmask = (request.readFDs | request.writeFDs | request.exceptFDs);
    pollfd fds[32];

    uint32_t fdcnt = 0;
    for (uint32_t handle = 0; handle < min(request.width, static_cast<uint32_t>(32)); handle++) {
        const uint32_t mask = static_cast<uint32_t>(1) << handle;
        if ((fdmask & mask) == 0) continue;

        const int fd = ResolveHandle(handle);
        if (fd == -1) continue;

        fds[fdcnt] = {.fd = fd, .events = 0, .revents = 0};
        if (mask & request.readFDs) fds[fdcnt].events |= POLLRDNORM;
        if (mask & request.writeFDs) fds[fdcnt].events |= POLLWRNORM;
        if (mask & request.exceptFDs) fds[fdcnt].events |= (POLLERR | POLLHUP);

        fdcnt++;
    }

    switch (withRetry(poll, fds, fdcnt, normalizeTimeout(request.timeout))) {
        case 0:
            resp.err = NetworkCodes::netErrTimeout;
            return;

        case -1:
            resp.err = NetworkCodes::errnoToPalm(errno);
            return;

        default:
            break;
    }

    for (uint32_t i = 0; i < fdcnt; i++) {
        const auto handle = ResolveSock(fds[i].fd);
        if (!handle) {
            cerr << "BUG: unable to resolve socket to handle!" << endl;
            continue;
        }

        if (fds[i].revents & POLLRDNORM) resp.readFDs |= (1 << *handle);
        if (fds[i].revents & POLLWRNORM) resp.writeFDs |= (1 << *handle);
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) resp.exceptFDs |= (1 << *handle);
    }
}

void NetworkSession::HandleSocketSend(MsgSocketSendRequest& request, const Buffer& sendPayload,
                                      MsgResponse& response) {
    response.which_payload = MsgResponse_socketSendResponse_tag;
    auto& resp = response.payload.socketSendResponse;

    resp.bytesSent = 0;
    resp.err = 0;

    const int sock = ResolveHandle(request.handle);
    if (sock == -1) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (sendPayload.size == 0) return;

    const SocketContext& ctx = *sockets[request.handle];
    const int32_t timeout = normalizeTimeout(request.timeout);
    const int flags = translateIOFlags(request.flags) & ~MSG_PEEK;

    sockaddr_in saddr;
    if (request.has_address) decodeSockaddr(request.address, saddr);

    int64_t timestampStart = timestampMsec();

    while (true) {
        const void* sendBuf = sendPayload.data.get() + resp.bytesSent;
        const size_t sendSize = sendPayload.size - resp.bytesSent;

        const ssize_t sendResult =
            request.has_address ? withRetry(sendto, sock, sendBuf, sendSize, flags,
                                            reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr))
                                : withRetry(send, sock, sendBuf, sendSize, flags);

        if (sendResult != -1) {
            resp.bytesSent += sendResult;
        } else if (errno != EAGAIN || !ctx.blocking) {
            resp.err = NetworkCodes::errnoToPalm(errno);
            return;
        }

        if (!ctx.blocking) return;

        const int64_t now = timestampMsec();
        if (now - timestampStart >= timeout ||
            resp.bytesSent >= static_cast<int32_t>(sendPayload.size))
            return;

        pollfd fds[] = {{.fd = sock, .events = 0, .revents = 0}};
        fds[0].events = POLLERR | POLLHUP | ((flags & MSG_OOB) ? POLLWRBAND : POLLWRNORM);

        switch (withRetry(poll, fds, 1, timeout - (now - timestampStart))) {
            case -1:
                cerr << "poll failed during send: " << errno << endl;
                resp.err = NetworkCodes::netErrInternal;
                return;

            case 0:
                if (resp.bytesSent == 0) resp.err = NetworkCodes::netErrTimeout;
                return;

            default:
                break;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            resp.err = getSocketError(sock);
            return;
        }
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

std::optional<uint32_t> NetworkSession::ResolveSock(int sock) const {
    for (uint32_t handle = 0; handle <= MAX_HANDLE; handle++)
        if (sockets[handle] && sockets[handle]->sock == sock) return handle;

    return nullopt;
}

NetworkSession::SocketContext::SocketContext(int sock) : sock(sock) {}

void NetworkSession::SendResponse(MsgResponse& response, size_t size) {
    if (rpcResponse.size() < size) rpcResponse.resize(size);
    pb_ostream_t stream = pb_ostream_from_buffer(rpcResponse.data(), size);

    pb_encode(&stream, MsgResponse_fields, &response);

    {
        unique_lock<mutex> lock(dispatchMutex);
        rpcRequestPending = false;
    }

    resultCb(rpcResponse.data(), stream.bytes_written);
}

bool NetworkSession::bufferDecodeCb(pb_istream_t* stream, const pb_field_iter_t* field,
                                    void** arg) {
    if (!arg) return false;
    auto buffer = reinterpret_cast<Buffer*>(*arg);

    buffer->size = stream->bytes_left;
    buffer->data = make_unique<uint8_t[]>(buffer->size);

    return pb_read(stream, buffer->data.get(), buffer->size);
}

bool NetworkSession::payloadDecodeCb(pb_istream_t* stream, const pb_field_iter_t* field,
                                     void** arg) {
    if (!arg) return false;
    if (field->tag != MsgRequest_socketSendRequest_tag) return true;

    auto request = reinterpret_cast<MsgSocketSendRequest*>(field->pData);

    request->data.arg = *arg;
    request->data.funcs.decode = bufferDecodeCb;

    return true;
}