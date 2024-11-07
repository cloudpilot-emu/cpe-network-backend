#ifndef _NETWORK_SOCKOPT_H_
#define _NETWORK_SOCKOPT_H_

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>

#include "networking.pb.h"

namespace NetworkSockopt {
    struct SockoptParameters {
        int level;
        int name;
        socklen_t len;

        union {
            int intval;
            uint8_t bufval[40];
            struct linger linger;
        } payload;
    };

    bool translateSetSockoptParameters(const MsgSocketOptionSetRequest& request,
                                       SockoptParameters& params);

    bool translateGetSockoptParameters(const MsgSocketOptionGetRequest& request,
                                       SockoptParameters& params);

    void translateGetSockoptResponse(const SockoptParameters& params,
                                     MsgSocketOptionGetResponse& response);
}  // namespace NetworkSockopt

#endif