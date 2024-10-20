#include "NetworkSession.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>

#include "networking.pb.h"
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

bool NetworkSession::DispatchRpc(void* data, size_t len) {
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

        HandleRpcRequest();
    }

    worker.detach();
    hasTerminated = true;
}

void NetworkSession::HandleRpcRequest() {
    MsgResponse response = MsgResponse_init_zero;

    response.which_payload = MsgResponse_invalidRequestResponse_tag;
    response.payload.invalidRequestResponse.tag = true;

    SendResponse(response, RESPONSE_STATIC_SIZE);
}

void NetworkSession::SendResponse(MsgResponse& response, size_t size) {
    if (rpcResponse.size() < size) rpcResponse.resize(size);
    pb_ostream_t stream = pb_ostream_from_buffer(rpcResponse.data(), size);

    pb_encode(&stream, MsgResponse_fields, &response);

    resultCb(rpcResponse.data(), stream.bytes_written);
}
