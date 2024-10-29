#ifndef NETWORK_BACKEND_H
#define NETWORK_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*net_RpcResultCb)(uint32_t sessiondId, const uint8_t* data, size_t len,
                                void* context);

void net_setRpcCallback(net_RpcResultCb resultCb, void* context);

unsigned int net_openSession();

void net_closeSession(uint32_t sessionId);
void net_closeAllSessions();

bool net_dispatchRpc(uint32_t sessionId, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // NETWORK_BACKEND_H
