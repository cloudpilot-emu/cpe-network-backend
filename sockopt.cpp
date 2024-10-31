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

            case netSocketOptSockUseLoopback:
                return SO_USELOOPBACK;

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

            case netSocketOptSockSndTimeout:
                return SO_SNDTIMEO;

            case netSocketOptSockRcvTimeout:
                return SO_RCVTIMEO;

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
        params.len = min(static_cast<pb_size_t>(40), request.value.bufval.size);
        memcpy(&params.payload.bufval, request.value.bufval.bytes, params.len);
    }

    switch (request.level) {
        case netSocketOptLevelSocket: {
            params.level = SOL_SOCKET;

            auto translatedOption = translateSocketOption_levelSock(request.option);
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

            auto translatedOption = translateSocketOption_levelTcp(request.option);
            if (!translatedOption) return false;

            params.name = *translatedOption;

            break;
        }
    }

    return true;
}
