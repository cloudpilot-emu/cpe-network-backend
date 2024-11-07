#include "codes.h"

#include <netdb.h>

#include <cerrno>
#include <iostream>

using namespace std;

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

uint16_t NetworkCodes::gaiErrorToPalm(int err) {
    switch (err) {
        case EAI_NONAME:
            return netErrDNSUnreachable;

        case EAI_AGAIN:
            return netErrDNSServerFailure;

        case EAI_FAIL:
            return netErrDNSRefused;

        case EAI_NODATA:
            return netErrDNSNonexistantName;

        default:
            return netErrInternal;
    }
}
