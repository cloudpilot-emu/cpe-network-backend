#include "codes.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <netdb.h>
    #include <sys/socket.h>
#endif

#include <cerrno>
#include <iostream>

using namespace std;

#ifdef _WIN32

uint16_t NetworkCodes::errnoToPalm(int err) {
    switch (err) {
        case WSAEINTR:
            return netErrUserCancel;

        case WSAENOMEM:
        case WSAENOBUFS:
            return netErrOutOfMemory;

        case WSAEACCES:
            return netErrAuthFailure;

        case WSAEWOULDBLOCK:
            return netErrWouldBlock;

        case WSAEINPROGRESS:
            return netErrWouldBlock;

        case WSAEALREADY:
            return netErrAlreadyInProgress;

        case WSAENOTSOCK:
            return netErrNoSocket;

        case WSAEDESTADDRREQ:
            return netErrIPNoDst;

        case WSAEMSGSIZE:
            return netErrMessageTooBig;

        case WSAENOPROTOOPT:
        case WSAEPROTONOSUPPORT:
            return netErrUnknownProtocol;

        case WSAESOCKTNOSUPPORT:
        case WSAEOPNOTSUPP:
            return netErrWrongSocketType;

        case WSAEPFNOSUPPORT:
        case WSAEAFNOSUPPORT:
            return netErrUnknownService;

        case WSAEADDRINUSE:
        case WSAEADDRNOTAVAIL:
            return netErrPortInUse;

        case WSAENETDOWN:
            return netErrUnreachableDest;

        case WSAENETUNREACH:
            return netErrNoInterfaces;

        case WSAENETRESET:
        case WSAECONNABORTED:
        case WSAECONNRESET:
            return netErrSocketClosedByRemote;

        case WSAEISCONN:
            return netErrSocketAlreadyConnected;

        case WSAENOTCONN:
            return netErrSocketNotConnected;

        case WSAESHUTDOWN:
            return netErrSocketNotOpen;

        case WSAETIMEDOUT:
        case WSAECONNREFUSED:
            return netErrTimeout;

        case WSAEHOSTDOWN:
        case WSAEHOSTUNREACH:
            return netErrIPNoRoute;

        default:
            cerr << "unhandled WSA error " << err << " mapped to netErrInternal" << endl;
            return netErrInternal;
    }
}

#else

uint16_t NetworkCodes::errnoToPalm(int err) {
    if (err == EWOULDBLOCK) return netErrWouldBlock;

    switch (err) {
        case EINTR:
            return netErrUserCancel;

        case EDEADLK:
            return netErrWouldBlock;

        case ENOMEM:
            return netErrOutOfMemory;

        case EACCES:
            return netErrAuthFailure;

        case EBUSY:
            return netErrSocketBusy;

        case EROFS:
            return netErrReadOnlySetting;

        case EAGAIN:
            return netErrWouldBlock;

        case EINPROGRESS:
            return netErrWouldBlock;

        case EALREADY:
            return netErrAlreadyInProgress;

        case ENOTSOCK:
            return netErrNoSocket;

        case EDESTADDRREQ:
            return netErrIPNoDst;

        case EMSGSIZE:
            return netErrMessageTooBig;

        case ENOPROTOOPT:
            return netErrUnknownProtocol;

        case EPROTONOSUPPORT:
            return netErrUnknownProtocol;

        case ESOCKTNOSUPPORT:
            return netErrWrongSocketType;

        case EOPNOTSUPP:
            return netErrWrongSocketType;

        case EPFNOSUPPORT:
            return netErrUnknownService;

        case EAFNOSUPPORT:
            return netErrUnknownService;

        case EADDRINUSE:
            return netErrPortInUse;

        case EADDRNOTAVAIL:
            return netErrPortInUse;

        case ENETDOWN:
            return netErrUnreachableDest;

        case ENETUNREACH:
            return netErrNoInterfaces;

        case ENETRESET:
            return netErrSocketClosedByRemote;

        case ECONNABORTED:
            return netErrSocketClosedByRemote;

        case ECONNRESET:
            return netErrSocketClosedByRemote;

        case ENOBUFS:
            return netErrNoTCB;

        case EISCONN:
            return netErrSocketAlreadyConnected;

        case ENOTCONN:
            return netErrSocketNotConnected;

        case ESHUTDOWN:
            return netErrSocketNotOpen;

        case ETIMEDOUT:
            return netErrTimeout;

        case ECONNREFUSED:
            return netErrTimeout;

        case EHOSTDOWN:
            return netErrIPNoRoute;

        case EHOSTUNREACH:
            return netErrIPNoRoute;

        case EPIPE:
            return netErrSocketClosedByRemote;

        default:
            cerr << "unhandled errno " << err << " mapped to netErrInternal" << endl;
            return netErrInternal;
    }
}

#endif

uint16_t NetworkCodes::gaiErrorToPalm(int err) {
    switch (err) {
        case EAI_NONAME:
            return netErrDNSUnreachable;

        case EAI_AGAIN:
            return netErrDNSServerFailure;

        case EAI_FAIL:
            return netErrDNSRefused;

#ifdef EAI_NODATA
        case EAI_NODATA:
            return netErrDNSNonexistantName;
#endif

        default:
            return netErrInternal;
    }
}
