#include "sockopt.h"

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <string.h>

#include <algorithm>
#include <optional>

#include "codes.h"

using namespace NetworkCodes;
using namespace std;

namespace {
    optional<int> translateSocketOption_levelSock(uint16_t option) {
        switch (option) {
            case netSocketOptSockDebug:
                return SO_DEBUG;

            case netSocketOptSockAcceptConn:
                return SO_ACCEPTCONN;

            case netSocketOptSockReuseAddr:
                return SO_REUSEADDR;

            case netSocketOptSockKeepAlive:
                return SO_KEEPALIVE;

            case netSocketOptSockDontRoute:
                return SO_DONTROUTE;

            case netSocketOptSockBroadcast:
                return SO_BROADCAST;

#ifdef SO_USELOOPBACK
            case netSocketOptSockUseLoopback:
                return SO_USELOOPBACK;
#endif

            case netSocketOptSockLinger:
                return SO_LINGER;

            case netSocketOptSockOOBInLine:
                return SO_OOBINLINE;

            case netSocketOptSockSndBufSize:
                return SO_SNDBUF;

            case netSocketOptSockRcvBufSize:
                return SO_RCVBUF;

            case netSocketOptSockSndLowWater:
                return SO_SNDLOWAT;

            case netSocketOptSockRcvLowWater:
                return SO_RCVLOWAT;

            case netSocketOptSockErrorStatus:
                return SO_ERROR;

            case netSocketOptSockSocketType:
                return SO_TYPE;

            default:
                return nullopt;
        }
    }

    optional<int> translateSocketOption_levelTcp(uint16_t option) {
        switch (option) {
            case netSocketOptTCPNoDelay:
                return TCP_NODELAY;

            case netSocketOptTCPMaxSeg:
                return TCP_MAXSEG;

            default:
                return nullopt;
        }
    }
}  // namespace

bool NetworkSockopt::translateSetSockoptParameters(const MsgSocketOptionSetRequest& request,
                                                   SockoptParameters& params) {
    if (request.which_value == MsgSocketOptionSetRequest_intval_tag) {
        params.len = sizeof(int);
        params.payload.intval = request.value.intval;
    } else {
        params.len = static_cast<socklen_t>(min(sizeof(request.value.bufval.bytes),
                                                static_cast<size_t>(request.value.bufval.size)));

        memcpy(&params.payload.bufval, request.value.bufval.bytes, params.len);
    }

    switch (request.level) {
        case netSocketOptLevelSocket: {
            params.level = SOL_SOCKET;

            const auto translatedOption = translateSocketOption_levelSock(request.option);
            if (!translatedOption) return false;

            params.name = *translatedOption;
            if (params.name == SO_LINGER) {
                params.payload.linger.l_onoff = request.value.intval & 0xffff;
                params.payload.linger.l_linger = (request.value.intval >> 16) & 0xffff;

                params.len = sizeof(params.payload.linger);
            }

            break;
        }

        case netSocketOptLevelIP:
            params.level = IPPROTO_IP;

            if (request.option == netSocketOptIPOptions)
                params.name = IP_OPTIONS;
            else
                return false;

            break;

        case netSocketOptLevelTCP: {
            params.level = IPPROTO_TCP;

            const auto translatedOption = translateSocketOption_levelTcp(request.option);
            if (!translatedOption) return false;

            params.name = *translatedOption;

            break;
        }
    }

    return true;
}

bool NetworkSockopt::translateGetSockoptParameters(const MsgSocketOptionGetRequest& request,
                                                   SockoptParameters& params) {
    switch (request.level) {
        case netSocketOptLevelSocket: {
            params.level = SOL_SOCKET;

            const auto translatedOption = translateSocketOption_levelSock(request.option);
            if (!translatedOption) return false;

            params.name = *translatedOption;
            params.len = params.name == SO_LINGER ? sizeof(params.payload.linger)
                                                  : sizeof(params.payload.intval);

            break;
        }

        case netSocketOptLevelIP:
            params.level = IPPROTO_IP;

            if (request.option == netSocketOptIPOptions)
                params.name = IP_OPTIONS;
            else
                return false;

            params.len = sizeof(params.payload.bufval);

            break;

        case netSocketOptLevelTCP: {
            params.level = IPPROTO_TCP;

            const auto translatedOption = translateSocketOption_levelTcp(request.option);
            if (!translatedOption) return false;

            params.name = *translatedOption;
            params.len = sizeof(params.payload.intval);

            break;
        }
    }

    return true;
}

void NetworkSockopt::translateGetSockoptResponse(const SockoptParameters& params,
                                                 MsgSocketOptionGetResponse& response) {
    if (params.level == SOL_SOCKET && params.name == SO_LINGER) {
        response.which_value = MsgSocketOptionGetResponse_intval_tag;
        response.value.intval = (params.payload.linger.l_onoff & 0xffff) |
                                ((params.payload.linger.l_linger & 0xffff) << 16);
    } else if (params.level == IPPROTO_IP && params.name == IP_OPTIONS) {
        response.which_value = MsgSocketOptionGetResponse_bufval_tag;
        response.value.bufval.size = params.len;

        memcpy(response.value.bufval.bytes, params.payload.bufval,
               min(min(sizeof(response.value.bufval.bytes), sizeof(params.payload.bufval)),
                   static_cast<size_t>(params.len)));
    } else {
        response.which_value = MsgSocketOptionGetResponse_intval_tag;
        response.value.intval = params.payload.intval;
    }
}
