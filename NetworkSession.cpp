#include "NetworkSession.h"

#include <cstddef>
#include <cstring>
#include <iostream>

#include "pb_decode.h"
#include "pb_encode.h"

using namespace std;

namespace {
    constexpr size_t INITIAL_SIZE_REQUEST = 1024;
    constexpr size_t INITIAL_SIZE_RESPONSE = 1024;

    constexpr size_t RESPONSE_STATIC_SIZE = 128;
}  // namespace

NetworkSession::NetworkSession(RpcResultCb resultCb)
    : resultCb(resultCb), rpcRequest(INITIAL_SIZE_REQUEST), rpcResponse(INITIAL_SIZE_RESPONSE) {}

void NetworkSession::Start() {
    if (hasStarted) return;

    worker = thread(bind(&NetworkSession::WorkerMain, this));
    hasStarted = true;
}

void NetworkSession::Terminate() {
    unique_lock lock(dispatchMutex);

    terminateRequested = true;
    dispatchCv.notify_one();
}

bool NetworkSession::DispatchRpc(const uint8_t* data, size_t len) {
    unique_lock lock(dispatchMutex);

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
            unique_lock lock(dispatchMutex);

            rpcRequestPending = false;
            while (!rpcRequestPending && !terminateRequested) dispatchCv.wait(lock);

            if (terminateRequested) break;
        }

        pb_istream_t stream = pb_istream_from_buffer(rpcRequest.data(), rpcRequestSize);
        MsgRequest request;

        if (!pb_decode(&stream, MsgRequest_fields, &request)) {
            cerr << "failed to decode RPC request" << endl;
        }

        HandleRpcRequest(request);
    }

    worker.detach();
    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest(MsgRequest& request) {
    MsgResponse response = MsgResponse_init_zero;

    response.which_payload = MsgResponse_invalidRequestResponse_tag;

    response.id = request.id;
    response.payload.invalidRequestResponse.tag = true;

    SendResponse(response, RESPONSE_STATIC_SIZE);
}

void NetworkSession::SendResponse(MsgResponse& response, size_t size) {
    if (rpcResponse.size() < size) rpcResponse.resize(size);
    pb_ostream_t stream = pb_ostream_from_buffer(rpcResponse.data(), size);

    pb_encode(&stream, MsgResponse_fields, &response);

    resultCb(rpcResponse.data(), stream.bytes_written);
}
