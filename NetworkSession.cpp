#include "NetworkSession.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "codes.h"
#include "pb_decode.h"
#include "pb_encode.h"

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
    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest(MsgRequest& request) {
    MsgResponse response = MsgResponse_init_zero;

    switch (request.which_payload) {
        case MsgRequest_socketOpenRequest_tag:
            HandleSocketOpen(request.payload.socketOpenRequest, response);
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

    int32_t handle = getFreeHandle();
    if (handle < 0) {
        LOG("not free handles left");
        resp.err = NetworkCodes::netErrInternal;
        return;
    }

    int fd = socket(AF_INET, socketType, 0);
    if (fd == -1) {
        resp.err = NetworkCodes::errnoToPalm(errno);
        return;
    }

    sockets[handle] = {.fd = fd};
    resp.handle = handle;
}

int32_t NetworkSession::getFreeHandle() {
    for (size_t i = 0; i < sockets.size(); i++)
        if (!sockets[i]) return i;

    return -1;
}

void NetworkSession::SendResponse(MsgResponse& response, size_t size) {
    if (rpcResponse.size() < size) rpcResponse.resize(size);
    pb_ostream_t stream = pb_ostream_from_buffer(rpcResponse.data(), size);

    pb_encode(&stream, MsgResponse_fields, &response);

    resultCb(rpcResponse.data(), stream.bytes_written);
}
