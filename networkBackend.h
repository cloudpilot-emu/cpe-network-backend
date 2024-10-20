#ifndef NETWORK_BACKEND_H
#define NETWORK_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*net_RpcResultCb)(unsigned int sessiondId, void* data, size_t len, void* context);

void net_setRpcCallback(net_RpcResultCb resultCb, void* context);

unsigned int net_openSession();
void net_closeSession(unsigned int sessionId);

bool net_dispatchRpc(unsigned int sessionId, void* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // NETWORK_BACKEND_H