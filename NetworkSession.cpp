#include "NetworkSession.h"

#ifdef _WIN32
// clang-format off
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
// clang-format on
#else
    #include <arpa/inet.h>
    #include <arpa/nameser.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <resolv.h>
    #include <signal.h>
    #include <sys/param.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>

#include "codes.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "sockopt.h"

using namespace std;

#ifdef _WIN32
    #define poll WSAPoll

    #define SHUT_RD SD_RECEIVE
    #define SHUT_WR SD_SEND
    #define SHUT_RDWR SD_BOTH

    #define SOCK_ERRNO WSAGetLastError()

    #define SOCK_EINTR WSAEINTR
    #define SOCK_EINPROGRESS WSAEINPROGRESS
    #define SOCK_EALREADY WSAEALREADY
    #define SOCK_EAGAIN WSAEWOULDBLOCK
    #define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
    #define SOCK_EPIPE WSAECONNRESET
#else
    #define SOCK_ERRNO errno

    #define SOCK_EINTR EINTR
    #define SOCK_EINPROGRESS EINPROGRESS
    #define SOCK_EALREADY EALREADY
    #define SOCK_EAGAIN EAGAIN
    #define SOCK_EWOULDBLOCK EWOULDBLOCK
    #define SOCK_EPIPE EPIPE
#endif

#ifdef NETWORK_BACKEND_LOGGING
    #define LOGGING
#endif

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
    constexpr size_t RESPONSE_STATIC_SIZE = 1024;
    constexpr uint32_t MAX_TIMEOUT = 10000;
    constexpr uint32_t MAX_RECEIVE_LEN = 0xffff;

    class Defer {
       public:
        Defer(std::function<void()> deferCb) : deferCb(deferCb) {}

        ~Defer() { deferCb(); }

       private:
        std::function<void()> deferCb;

       private:
        Defer(const Defer&) = delete;
        Defer(Defer&&) = delete;
        Defer& operator=(const Defer&) = delete;
        Defer& operator=(Defer&&) = delete;
    };

    template <typename F, typename... Ts>
    auto withRetry(F fn, Ts... args) {
        decltype(fn(args...)) result;
        do {
            result = fn(args...);
        } while (result == -1 && SOCK_ERRNO == SOCK_EINTR);

        return result;
    }

    int closeSocket(sock_t sock) {
#ifdef _WIN32
        return closesocket(sock);
#elif defined(__APPLE__)
        return withRetry(close, sock);
#else
        const int result = close(sock);
        if (result == -1 && errno == EINTR) return 0;

        return result;
#endif
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

    int shutdownHow(uint16_t direction) {
        switch (direction) {
            case NetworkCodes::netSocketDirInput:
                return SHUT_RD;

            case NetworkCodes::netSocketDirOutput:
                return SHUT_WR;

            case NetworkCodes::netSocketDirBoth:
                return SHUT_RDWR;

            default:
                return 0;
        }
    }

    bool encodeSockaddr(const sockaddr* saddr, Address& addr, socklen_t len) {
        if (static_cast<size_t>(len) < sizeof(sockaddr_in) || saddr->sa_family != AF_INET) return false;
        const auto saddr4 = reinterpret_cast<const sockaddr_in*>(saddr);

        addr.ip = ntohl(saddr4->sin_addr.s_addr);
        addr.port = ntohs(saddr4->sin_port);

        return true;
    }

    bool encodeSockaddrIp(const sockaddr* saddr, uint32_t& addr, socklen_t len) {
        if (static_cast<size_t>(len) < sizeof(sockaddr_in) || saddr->sa_family != AF_INET)
            return false;
        const auto saddr4 = reinterpret_cast<const sockaddr_in*>(saddr);

        addr = ntohl(saddr4->sin_addr.s_addr);
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

    unique_ptr<sockaddr> translateAddress(Address& addr, size_t& addrLen) {
        ostringstream ip;
        ip << (addr.ip >> 24) << "." << ((addr.ip >> 16) & 0xff) << "." << ((addr.ip >> 8) & 0xff)
           << "." << (addr.ip & 0xff);

        ostringstream port;
        port << addr.port;

        addrinfo* result;
        addrinfo hints;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = PF_UNSPEC;
        hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;

        const int gaierr = getaddrinfo(ip.str().c_str(), port.str().c_str(), &hints, &result);

        if (gaierr != 0) {
            cerr << "failed to translate " << ip.str() << " : " << gai_strerror(gaierr) << endl;
            return unique_ptr<sockaddr>();
        }

        Defer freeResult([=]() { freeaddrinfo(result); });

        if (!result->ai_addr) {
            cerr << ip.str() << " did not translate to a valid address" << endl;
            return unique_ptr<sockaddr>();
        }

#ifdef LOGGING
        char buffer[64];
        memset(buffer, 0, sizeof(buffer));

        switch (result->ai_addr->sa_family) {
            case AF_INET:
                inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(result->ai_addr)->sin_addr,
                          buffer, sizeof(buffer));
                LOG("translated %s to %s port %i\n", ip.str().c_str(), buffer,
                    ntohs(reinterpret_cast<const sockaddr_in*>(result->ai_addr)->sin_port));

                break;

            case AF_INET6:
                inet_ntop(AF_INET6,
                          &reinterpret_cast<const sockaddr_in6*>(result->ai_addr)->sin6_addr,
                          buffer, sizeof(buffer));
                LOG("translated %s to %s port %i\n", ip.str().c_str(), buffer,
                    ntohs(reinterpret_cast<const sockaddr_in6*>(result->ai_addr)->sin6_port));

                break;

            default:
                LOG("translated %s to unknown address family %i\n", ip.str().c_str(),
                    result->ai_addr->sa_family);

                break;
        }
#endif

        addrLen = result->ai_addrlen;
        void* addrClone = malloc(addrLen);
        memcpy(addrClone, result->ai_addr, addrLen);

        return unique_ptr<sockaddr>(reinterpret_cast<sockaddr*>(addrClone));
    }

    bool setNonBlocking(sock_t sock) {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
        int flags = withRetry(fcntl, sock, F_GETFL);
        if (flags == -1) return false;

        flags |= O_NONBLOCK;

        return withRetry(fcntl, sock, F_SETFL, flags) != -1;
#endif
    }

    uint16_t getSocketError(sock_t sock) {
        int err;
        socklen_t len = sizeof(err);

        if (withRetry(getsockopt, sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err),
                      &len) == -1) {
            auto const err = SOCK_ERRNO;
            cerr << "unable to retrieve socket error: " << err << endl;

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

#ifdef LOGGING
    string formatAddress(const Address& addr) {
        ostringstream s;

        s << (addr.ip >> 24) << "." << ((addr.ip >> 16) & 0xff) << "." << ((addr.ip >> 8) & 0xff)
          << "." << (addr.ip & 0xff) << ":" << addr.port;

        return s.str();
    }
#endif
}  // namespace

NetworkSession::NetworkSession(RpcResultCb resultCb)
    : resultCb(resultCb), rpcRequest(INITIAL_SIZE_REQUEST), rpcResponse(INITIAL_SIZE_RESPONSE) {}

void NetworkSession::Start() {
    if (hasStarted) return;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    thread(bind(&NetworkSession::WorkerMain, this)).detach();
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

void NetworkSession::SetDnsServers(uint32_t primary, uint32_t secondary) {
    unique_lock lock(dnsSettingsMutex);

    dnsSettings = {.primary = primary, .secondary = secondary};
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

    for (const auto& ctx : sockets) {
        if (!ctx) continue;

        LOG("cleaning out socket %i\n", ctx->sock);

        withRetry(shutdown, ctx->sock, SHUT_RDWR);
        closeSocket(ctx->sock);
    }

    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest(MsgRequest& request, const Buffer& payloadBuffer) {
    MsgResponse response = MsgResponse_init_zero;
    Buffer responseBuffer;

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

        case MsgRequest_socketOptionGetRequest_tag:
            HandleSocketOptionGet(request.payload.socketOptionGetRequest, response);
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

        case MsgRequest_socketReceiveRequest_tag:
            HandleSocketReceive(request.payload.socketReceiveRequest, &responseBuffer, response);
            break;

        case MsgRequest_settingGetRequest_tag:
            HandleSettingsGet(request.payload.settingGetRequest, response);
            break;

        case MsgRequest_getHostByNameRequest_tag:
            HandleGetHostByName(request.payload.getHostByNameRequest, response);
            break;

        case MsgRequest_getServByNameRequest_tag:
            HandleGetServByName(request.payload.getServByNameRequest, response);
            break;

        case MsgRequest_socketShutdownRequest_tag:
            HandleSocketShutdown(request.payload.socketShutdownRequest, response);
            break;

        case MsgRequest_socketListenRequest_tag:
            HandleSocketListen(request.payload.socketListenRequest, response);
            break;

        case MsgRequest_socketAcceptRequest_tag:
            HandleSocketAccept(request.payload.socketAcceptRequest, response);
            break;

        default:
            cerr << "unhandled RPC payload type " << request.which_payload << endl;
            response.which_payload = MsgResponse_invalidRequestResponse_tag;
            response.payload.invalidRequestResponse.tag = true;
            break;
    }

    response.id = request.id;

    SendResponse(response, RESPONSE_STATIC_SIZE + responseBuffer.size);
}

void NetworkSession::HandleSocketOpen(MsgSocketOpenRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketOpenResponse_tag;
    auto& resp = response.payload.socketOpenResponse;

    resp.handle = -1;
    resp.err = 0;

    LOG("SocketOpen type %i protocol %i\n", request.type, request.protocol);

#ifdef LOGGING
    Defer logResult([&]() { LOG("SocketOpen result err %i handle %i\n", resp.err, resp.handle); });
#endif

    int socketType = mapSocketType(request.type);
    if (socketType < 0) {
        LOG("bad socket type %i\n", request.type);
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (socketType == SOCK_RAW) {
        LOG("RAW sockets are not unsupported\n");
        resp.err = NetworkCodes::netErrUnimplemented;
        return;
    }

    int32_t handle = GetFreeHandle();
    if (handle < 0) {
        LOG("not free handles left");
        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    auto sock = withRetry(socket, AF_INET, socketType, 0);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
        return;
    }

    if (!setNonBlocking(sock)) {
        auto const err = SOCK_ERRNO;
        cerr << "failed to set socket non-blocking: " << err << endl;
        closeSocket(sock);

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

    LOG("SocketClose handle %i\n", request.handle);

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    sockets[request.handle] = nullopt;

    withRetry(shutdown, sock, SHUT_RDWR);
    if (closeSocket(sock) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
    }
}

void NetworkSession::HandleSocketOptionSet(MsgSocketOptionSetRequest& request,
                                           MsgResponse& response) {
    response.which_payload = MsgResponse_socketOptionSetResponse_tag;
    auto& resp = response.payload.socketOptionSetResponse;

    resp.err = 0;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
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

    if (withRetry(setsockopt, sock, parameters.level, parameters.name,
                  reinterpret_cast<const char*>(&parameters.payload), parameters.len) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
    }
}

void NetworkSession::HandleSocketOptionGet(MsgSocketOptionGetRequest& request,
                                           MsgResponse& respose) {
    respose.which_payload = MsgResponse_socketOptionGetResponse_tag;
    auto& resp = respose.payload.socketOptionGetResponse;

    resp.err = 0;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (request.level == NetworkCodes::netSocketOptLevelSocket &&
        request.option == NetworkCodes::netSocketOptSockNonBlocking) {
        resp.which_value = MsgSocketOptionGetResponse_intval_tag;
        resp.value.intval = !sockets[request.handle]->blocking;

        return;
    }

    NetworkSockopt::SockoptParameters parameters;
    if (!NetworkSockopt::translateGetSockoptParameters(request, parameters)) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (withRetry(getsockopt, sock, parameters.level, parameters.name,
                  reinterpret_cast<char*>(&parameters.payload), &parameters.len) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
        return;
    }

    NetworkSockopt::translateGetSockoptResponse(parameters, resp);
}

void NetworkSession::HandleSocketAddr(MsgSocketAddrRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketAddrResponse_tag;
    auto& resp = response.payload.socketAddrResponse;

    resp.err = 0;

    LOG("SocketAddr\n");

#ifdef LOGGING
    Defer logResult([&]() {
        LOG("SocketAddr local %s remote %s\n",
            resp.has_addressLocal ? formatAddress(resp.addressLocal).c_str() : "-",
            resp.has_addressRemote ? formatAddress(resp.addressRemote).c_str() : "-");
    });
#endif

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (request.requestAddressLocal) {
        sockaddr addr;
        socklen_t addrLen = sizeof(addr);

        if (withRetry(getsockname, sock, &addr, &addrLen) == -1) {
            resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
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
            resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
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

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    sockaddr_in saddr;
    decodeSockaddr(request.address, saddr);

    if (withRetry(::bind, sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
    }
}

void NetworkSession::HandleSocketConnect(MsgSocketConnectRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketConnectResponse_tag;
    auto& resp = response.payload.socketConnectResponse;

    resp.err = 0;

    LOG("SocketConnect to %s handle %i timeout %i\n", formatAddress(request.address).c_str(),
        request.handle, request.timeout);

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    const SocketContext& ctx = *sockets[request.handle];

    size_t saddrLen{0};
    auto saddr = translateAddress(request.address, saddrLen);
    if (!saddr) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (withRetry(connect, sock, reinterpret_cast<sockaddr*>(saddr.get()), saddrLen) == 0) return;

    if ((SOCK_ERRNO != SOCK_EINPROGRESS && SOCK_ERRNO != SOCK_EALREADY
        #ifdef _WIN32
           &&  SOCK_ERRNO != WSAEWOULDBLOCK
        #endif
    ) || !ctx.blocking) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
        return;
    }

    pollfd fds[] = {
        {.fd = sock, .events = POLLRDNORM | POLLWRNORM, .revents = 0}};

    switch (withRetry(poll, fds, 1, normalizeTimeout(request.timeout))) {
        case -1: {
            auto const err = SOCK_ERRNO;
            cerr << "poll failed during connect: " << err << endl;

            resp.err = NetworkCodes::netErrInternal;
            return;
        }
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

        const sock_t fd = SocketForHandle(handle);
        if (fd == INVALID_SOCK) continue;

        fds[fdcnt] = {.fd = fd, .events = 0, .revents = 0};
        if (mask & request.readFDs) fds[fdcnt].events |= POLLRDNORM;
        if (mask & request.writeFDs) fds[fdcnt].events |= POLLWRNORM;

        fdcnt++;
    }

    switch (withRetry(poll, fds, fdcnt, normalizeTimeout(request.timeout))) {
        case -1:
            resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
            return;

        default:
            break;
    }

    for (uint32_t i = 0; i < fdcnt; i++) {
        const auto handle = HandleForSocket(fds[i].fd);
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

    if (request.has_address) {
        LOG("SocketSendTo %s handle %i flags %i timeout %i\n",
            formatAddress(request.address).c_str(), request.handle, request.flags, request.timeout);
    } else {
        LOG("SocketSend handle %i flags %i timeout %i\n", request.handle, request.flags,
            request.timeout);
    }

#ifdef LOGGING
    Defer logResult(
        [&]() { LOG("SocketSend result bytesSend %i err %i\n", resp.bytesSent, resp.err); });
#endif

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (sendPayload.size == 0) return;

    const SocketContext& ctx = *sockets[request.handle];
    const int32_t timeout = normalizeTimeout(request.timeout);
    const int flags = translateIOFlags(request.flags) & ~MSG_PEEK;

    size_t saddrLen{0};
    unique_ptr<sockaddr> saddr;

    if (request.has_address) {
        saddr = translateAddress(request.address, saddrLen);

        if (!saddr) {
            resp.err = NetworkCodes::netErrParamErr;
            return;
        }
    }

    int64_t timestampStart = timestampMsec();

    while (true) {
        const void* sendBuf = sendPayload.data.get() + resp.bytesSent;
        const size_t sendSize = sendPayload.size - resp.bytesSent;

#ifdef _WIN32
        const int sendResult = request.has_address
                                   ? withRetry(sendto, sock, static_cast<const char*>(sendBuf),
                                               static_cast<int>(sendSize), flags, saddr.get(),
                                               static_cast<int>(saddrLen))
                                   : withRetry(send, sock, static_cast<const char*>(sendBuf),
                                               static_cast<int>(sendSize), flags);
#else
        const ssize_t sendResult =
            request.has_address
                ? withRetry(sendto, sock, sendBuf, sendSize, flags, saddr.get(), saddrLen)
                : withRetry(send, sock, sendBuf, sendSize, flags);
#endif

        if (sendResult != -1) {
            resp.bytesSent += sendResult;
        } else if (SOCK_ERRNO != SOCK_EAGAIN || !ctx.blocking) {
            resp.err = SOCK_ERRNO == SOCK_EPIPE ? 0 : NetworkCodes::errnoToPalm(SOCK_ERRNO);
            return;
        }

        if (!ctx.blocking || resp.bytesSent >= static_cast<int32_t>(sendPayload.size)) return;

        const int64_t now = timestampMsec();
        if (now - timestampStart >= timeout) {
            if (resp.bytesSent == 0) resp.err = NetworkCodes::netErrTimeout;
            return;
        }

        pollfd fds[] = {{.fd = sock, .events = 0, .revents = 0}};
        fds[0].events =  (flags & MSG_OOB) ? POLLWRBAND : POLLWRNORM;

        switch (withRetry(poll, fds, 1, static_cast<int>(timeout - (now - timestampStart)))) {
            case -1: {
                auto const err = SOCK_ERRNO;
                cerr << "poll failed during send: " << err << endl;

                resp.err = NetworkCodes::netErrInternal;
                return;
            }
            case 0:
                if (resp.bytesSent == 0) resp.err = NetworkCodes::netErrTimeout;
                return;

            default:
                break;
        }

        if (fds[0].revents & (POLLERR | POLLNVAL)) {
            resp.err = getSocketError(sock);
            return;
        }
    }
}

void NetworkSession::HandleSocketReceive(MsgSocketReceiveRequest& request, Buffer* receivePayload,
                                         MsgResponse& response) {
    response.which_payload = MsgRequest_socketReceiveRequest_tag;
    auto& resp = response.payload.socketReceiveResponse;

    resp.data.arg = receivePayload;
    resp.data.funcs.encode = bufferEncodeCb;
    resp.err = 0;
    resp.has_address = false;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (request.maxLen == 0) return;

    const SocketContext& ctx = *sockets[request.handle];
    const int32_t timeout = normalizeTimeout(request.timeout);
    const int flags = translateIOFlags(request.flags);
    sockaddr_in saddr;

    const size_t receiveLen = min(request.maxLen, MAX_RECEIVE_LEN);
    receivePayload->data = make_unique<uint8_t[]>(receiveLen);
    receivePayload->size = 0;

    int64_t timestampStart = timestampMsec();

    while (true) {
        void* recvBuf = receivePayload->data.get() + receivePayload->size;
        const size_t recvSize = receiveLen - receivePayload->size;
        socklen_t saddrLen = sizeof(saddr);

#ifdef _WIN32
        const int recvResult =
            request.addressRequested
                ? withRetry(recvfrom, sock, static_cast<char*>(recvBuf), static_cast<int>(recvSize),
                            flags, reinterpret_cast<sockaddr*>(&saddr), &saddrLen)
                : withRetry(recv, sock, static_cast<char*>(recvBuf), static_cast<int>(recvSize),
                            flags);
#else
        const ssize_t recvResult = request.addressRequested
                                       ? withRetry(recvfrom, sock, recvBuf, recvSize, flags,
                                                   reinterpret_cast<sockaddr*>(&saddr), &saddrLen)
                                       : withRetry(recv, sock, recvBuf, recvSize, flags);
#endif

        if (recvResult == 0) {
            break;
        } else if (recvResult != -1) {
            receivePayload->size += recvResult;
        } else if (SOCK_ERRNO != SOCK_EAGAIN || !ctx.blocking) {
            resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
            break;
        }

        if (!ctx.blocking || receivePayload->size > 0) break;

        const int64_t now = timestampMsec();
        if (now - timestampStart >= timeout) {
            if (receivePayload->size == 0) resp.err = NetworkCodes::netErrTimeout;
            break;
        }

        pollfd fds[] = {{.fd = sock, .events = 0, .revents = 0}};
        fds[0].events = (flags & MSG_OOB) ? POLLRDBAND : POLLRDNORM;

        const int pollResult =
            withRetry(poll, fds, 1, static_cast<int>(timeout - (now - timestampStart)));

        int err;
        switch (pollResult) {
            case -1:
                err = SOCK_ERRNO;
                cerr << "poll failed during read: " << err << endl;

                resp.err = NetworkCodes::netErrInternal;
                goto receive_finalize_response;

            case 0:
                if (receivePayload->size == 0) resp.err = NetworkCodes::netErrTimeout;
                goto receive_finalize_response;

            default:
                break;
        }

        if (fds[0].revents & (POLLERR | POLLNVAL)) {
            resp.err = getSocketError(sock);
            break;
        }
    }

receive_finalize_response:

    if (receivePayload->size > receiveLen) {
        cerr << "BUG: receive buffer overflow" << endl;
        receivePayload->size = receiveLen;
        resp.err = NetworkCodes::netErrInternal;
    }

    if (request.addressRequested && receivePayload->size > 0) {
        resp.has_address =
            encodeSockaddr(reinterpret_cast<sockaddr*>(&saddr), resp.address, sizeof(saddr));
    }
}

void NetworkSession::HandleSettingsGet(MsgSettingGetRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_settingGetResponse_tag;
    auto& resp = response.payload.settingGetResponse;

    switch (request.setting) {
        case NetworkCodes::netSettingHostName: {
            memset(resp.value.strval, 0, sizeof(resp.value.strval));

            if (withRetry(gethostname, resp.value.strval, sizeof(resp.value.strval)) == -1
#ifndef _WIN32
                && errno != ENAMETOOLONG
#endif
            ) {
                resp.err = NetworkCodes::netErrPrefNotFound;
                return;
            }

            resp.which_value = MsgSettingGetResponse_strval_tag;
            resp.value.strval[sizeof(resp.value.strval) - 1] = '\0';
            break;
        }

        case NetworkCodes::netSettingPrimaryDNS:
        case NetworkCodes::netSettingRTPrimaryDNS:
            HandleSettingsGetDNS(resp, 1);
            break;

        case NetworkCodes::netSettingSecondaryDNS:
        case NetworkCodes::netSettingRTSecondaryDNS:
            HandleSettingsGetDNS(resp, 2);
            break;

        default:
            resp.err = NetworkCodes::netErrPrefNotFound;
    }
}

void NetworkSession::HandleSettingsGetDNS(MsgSettingGetResponse& resp, size_t dnsLevel) {
    {
        unique_lock lock(dnsSettingsMutex);

        if (dnsSettings.has_value()) {
            resp.value.uint32val = dnsLevel == 1 ? dnsSettings->primary : dnsSettings->secondary;

            if (resp.value.uint32val == 0) {
                resp.err = NetworkCodes::netErrPrefNotFound;
            } else {
                resp.which_value = MsgSettingGetResponse_uint32val_tag;
            }

            return;
        }
    }

#ifdef _WIN32
    ULONG bufSize = sizeof(FIXED_INFO);
    unique_ptr<FIXED_INFO, decltype(&free)> fixedInfo(
        reinterpret_cast<FIXED_INFO*>(malloc(bufSize)), free);

    if (!fixedInfo) {
        resp.err = NetworkCodes::netErrPrefNotFound;
        return;
    }

    DWORD dwRet = GetNetworkParams(fixedInfo.get(), &bufSize);
    if (dwRet == ERROR_BUFFER_OVERFLOW) {
        fixedInfo.reset(reinterpret_cast<FIXED_INFO*>(malloc(bufSize)));
        if (!fixedInfo) {
            resp.err = NetworkCodes::netErrPrefNotFound;
            return;
        }
        dwRet = GetNetworkParams(fixedInfo.get(), &bufSize);
    }

    if (dwRet != NO_ERROR) {
        resp.err = NetworkCodes::netErrPrefNotFound;
        return;
    }

    bool found = false;
    size_t hits = 0;
    IP_ADDR_STRING* pAddr = &fixedInfo->DnsServerList;
    while (pAddr) {
        unsigned long ip = inet_addr(pAddr->IpAddress.String);
        if (ip != INADDR_NONE && ip != 0) {
            resp.value.uint32val = ntohl(ip);
            found = true;
            if (++hits == dnsLevel) break;
        }
        pAddr = pAddr->Next;
    }

    if (found) {
        resp.which_value = MsgSettingGetResponse_uint32val_tag;
        return;
    }
#elif !defined(__ANDROID__)
    if (res_init() == -1 || _res.nscount <= 0) {
        resp.err = NetworkCodes::netErrPrefNotFound;
        return;
    }

    bool found = false;
    size_t hits = 0;
    for (size_t i = 0; i < static_cast<size_t>(_res.nscount); i++) {
        if (_res.nsaddr_list[i].sin_family != AF_INET) continue;

        found = encodeSockaddrIp(reinterpret_cast<const sockaddr*>(&_res.nsaddr_list[i]),
                                 resp.value.uint32val, sizeof(_res.nsaddr_list[i])) ||
                found;
        if (++hits == dnsLevel) break;
    }

    if (found) {
        resp.which_value = MsgSettingGetResponse_uint32val_tag;
        return;
    }

#endif

    resp.err = NetworkCodes::netErrPrefNotFound;
}

void NetworkSession::HandleGetHostByName(MsgGetHostByNameRequest& request, MsgResponse& response) {
    response.which_payload = MsgRequest_getHostByNameRequest_tag;
    auto& resp = response.payload.getHostByNameResponse;

    resp.has_alias = false;

    addrinfo* result;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_CANONNAME;

    const int gaierr = getaddrinfo(request.name, nullptr, &hints, &result);
    if (gaierr != 0) {
        resp.err = NetworkCodes::gaiErrorToPalm(gaierr);
        return;
    }

    Defer freeResult([=]() { freeaddrinfo(result); });

    strncpy(resp.name, request.name, sizeof(resp.name));
    resp.name[sizeof(resp.name) - 1] = '\0';

    uint32_t i = 0;
    for (addrinfo* iter = result; iter != nullptr && i < 3; iter = iter->ai_next) {
        if (!encodeSockaddrIp(iter->ai_addr, resp.addresses[i], iter->ai_addrlen)) continue;

        if (iter->ai_canonname) {
            switch (i) {
                case 0:
                    strncpy(resp.name, iter->ai_canonname, sizeof(resp.name));
                    resp.name[sizeof(resp.name) - 1] = '\0';
                    break;

                case 1:
                    resp.has_alias = true;
                    strncpy(resp.alias, iter->ai_canonname, sizeof(resp.alias));
                    resp.alias[sizeof(resp.alias) - 1] = '\0';
                    break;

                default:
                    break;
            }
        }

        i++;
    }

    if (i == 0) {
        resp.err = NetworkCodes::netErrDNSNonexistantName;
        return;
    }

    resp.addresses_count = i;
}

void NetworkSession::HandleGetServByName(MsgGetServByNameRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_getServByNameResponse_tag;
    auto& resp = response.payload.getServByNameResponse;

    resp.err = 0;
    resp.port = 0;

    addrinfo* result;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
#ifdef AI_UNUSABLE
    hints.ai_flags = AI_UNUSABLE;
#else
    hints.ai_flags = 0;
#endif

    if (strncmp(request.protocol, "tcp", sizeof(request.protocol)) == 0) {
        hints.ai_protocol = IPPROTO_TCP;
    } else if (strncmp(request.protocol, "udp", sizeof(request.protocol)) == 0) {
        hints.ai_protocol = IPPROTO_UDP;
    } else {
        resp.err = NetworkCodes::netErrUnknownProtocol;
        return;
    }

    const int gaierr = getaddrinfo(nullptr, request.name, &hints, &result);
    if (gaierr != 0) {
        resp.err = NetworkCodes::gaiErrorToPalm(gaierr);
        return;
    }

    Defer freeResult([=]() { freeaddrinfo(result); });

    for (addrinfo* iter = result; iter != nullptr; iter = iter->ai_next) {
        if (iter->ai_addr->sa_family != AF_INET) continue;

        resp.port = ntohs(reinterpret_cast<sockaddr_in*>(iter->ai_addr)->sin_port);
        return;
    }

    resp.err = NetworkCodes::netErrUnknownService;
}

void NetworkSession::HandleSocketShutdown(MsgSocketShutdownRequest& request,
                                          MsgResponse& response) {
    response.which_payload = MsgResponse_socketShutdownResponse_tag;
    auto& resp = response.payload.socketShutdownResponse;

    resp.err = 0;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (withRetry(shutdown, sock, shutdownHow(request.direction)) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
    }
}

void NetworkSession::HandleSocketListen(MsgSocketListenRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketListenResponse_tag;
    auto& resp = response.payload.socketListenResponse;

    resp.err = 0;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    if (withRetry(listen, sock, request.backlog) == -1) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
    }
}

void NetworkSession::HandleSocketAccept(MsgSocketAcceptRequest& request, MsgResponse& response) {
    response.which_payload = MsgResponse_socketAcceptResponse_tag;
    auto& resp = response.payload.socketAcceptResponse;

    resp.err = 0;
    resp.handle = 0;

    const sock_t sock = SocketForHandle(request.handle);
    if (sock == INVALID_SOCK) {
        resp.err = NetworkCodes::netErrParamErr;
        return;
    }

    const SocketContext& ctx = *sockets[request.handle];
    pollfd fds[1];
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    auto incomingSock = withRetry(accept, sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);

    if (incomingSock != INVALID_SOCK) goto accept_return_socket;

    if (SOCK_ERRNO != SOCK_EWOULDBLOCK || !ctx.blocking) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
        return;
    }

    fds[0] = {.fd = sock, .events = POLLERR | POLLRDNORM, .revents = 0};

    switch (withRetry(poll, fds, 1, normalizeTimeout(request.timeout))) {
        case -1: {
            auto const err = SOCK_ERRNO;
            cerr << "poll failed during accept: " << err << endl;

            resp.err = NetworkCodes::netErrInternal;
            return;
        }
        case 0:
            resp.err = NetworkCodes::netErrTimeout;
            return;

        default:
            break;
    }

    if (fds[0].revents & (POLLERR | POLLNVAL)) {
        resp.err = getSocketError(sock);
        return;
    }

    addrLen = sizeof(addr);
    incomingSock = withRetry(accept, sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (incomingSock == INVALID_SOCK) {
        resp.err = NetworkCodes::errnoToPalm(SOCK_ERRNO);
        return;
    }

accept_return_socket:
    setNonBlocking(incomingSock);

    int16_t handle = GetFreeHandle();
    if (handle < 0) {
        cerr << "no free handles left during accept, shutting down disposing incoming socket"
             << endl;
        closeSocket(incomingSock);

        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    sockets[handle] = SocketContext(incomingSock);
    resp.handle = handle;
    encodeSockaddr(reinterpret_cast<const sockaddr*>(&addr), resp.address, addrLen);
}

int32_t NetworkSession::GetFreeHandle() {
    for (int32_t i = 1; i < static_cast<int32_t>(sockets.size()); i++) {
        if (!sockets[i]) return i;
    }

    return -1;
}

sock_t NetworkSession::SocketForHandle(uint32_t handle) const {
    if (handle > MAX_HANDLE) return INVALID_SOCK;

    const auto& socketContext = sockets[handle];
    if (!socketContext) return INVALID_SOCK;

    return socketContext->sock;
}

std::optional<uint32_t> NetworkSession::HandleForSocket(sock_t sock) const {
    for (uint32_t handle = 1; handle <= MAX_HANDLE; handle++) {
        if (sockets[handle] && sockets[handle]->sock == sock) return handle;
    }

    return nullopt;
}

bool NetworkSession::bufferEncodeCb(pb_ostream_t* stream, const pb_field_iter_t* field,
                                    void* const* arg) {
    if (!arg) return false;

    auto buffer = reinterpret_cast<const Buffer*>(*arg);

    pb_encode_tag_for_field(stream, field);
    return pb_encode_string(stream, buffer->data.get(), buffer->size);
}

NetworkSession::SocketContext::SocketContext(sock_t sock) : sock(sock) {}

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

bool NetworkSession::bufferDecodeCb(pb_istream_t* stream, const pb_field_iter_t*, void** arg) {
    if (!arg) return false;
    auto buffer = reinterpret_cast<Buffer*>(*arg);

    buffer->size = stream->bytes_left;
    buffer->data = make_unique<uint8_t[]>(buffer->size);

    return pb_read(stream, buffer->data.get(), buffer->size);
}

bool NetworkSession::payloadDecodeCb(pb_istream_t*, const pb_field_iter_t* field, void** arg) {
    if (!arg) return false;
    if (field->tag != MsgRequest_socketSendRequest_tag) return true;

    auto request = reinterpret_cast<MsgSocketSendRequest*>(field->pData);

    request->data.arg = *arg;
    request->data.funcs.decode = bufferDecodeCb;

    return true;
}
