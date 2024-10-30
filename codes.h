#ifndef __NETWORK_CODES_H__
#define __NETWORK_CODES_H__

#include <cstdint>

namespace NetworkCodes {
    constexpr uint16_t netErrorClass = 0x1200;

    constexpr uint16_t netErrAlreadyOpen = netErrorClass | 1;
    constexpr uint16_t netErrNotOpen = netErrorClass | 2;
    constexpr uint16_t netErrStillOpen = netErrorClass | 3;
    constexpr uint16_t netErrParamErr = netErrorClass | 4;
    constexpr uint16_t netErrNoMoreSockets = netErrorClass | 5;
    constexpr uint16_t netErrOutOfResources = netErrorClass | 6;
    constexpr uint16_t netErrOutOfMemory = netErrorClass | 7;
    constexpr uint16_t netErrSocketNotOpen = netErrorClass | 8;
    constexpr uint16_t netErrSocketBusy = netErrorClass | 9;
    constexpr uint16_t netErrMessageTooBig = netErrorClass | 10;
    constexpr uint16_t netErrSocketNotConnected = netErrorClass | 11;
    constexpr uint16_t netErrNoInterfaces = netErrorClass | 12;
    constexpr uint16_t netErrBufTooSmall = netErrorClass | 13;
    constexpr uint16_t netErrUnimplemented = netErrorClass | 14;
    constexpr uint16_t netErrPortInUse = netErrorClass | 15;
    constexpr uint16_t netErrQuietTimeNotElapsed = netErrorClass | 16;
    constexpr uint16_t netErrInternal = netErrorClass | 17;
    constexpr uint16_t netErrTimeout = netErrorClass | 18;
    constexpr uint16_t netErrSocketAlreadyConnected = netErrorClass | 19;
    constexpr uint16_t netErrSocketClosedByRemote = netErrorClass | 20;
    constexpr uint16_t netErrOutOfCmdBlocks = netErrorClass | 21;
    constexpr uint16_t netErrWrongSocketType = netErrorClass | 22;
    constexpr uint16_t netErrSocketNotListening = netErrorClass | 23;
    constexpr uint16_t netErrUnknownSetting = netErrorClass | 24;
    constexpr uint16_t netErrInvalidSettingSize = netErrorClass | 25;
    constexpr uint16_t netErrPrefNotFound = netErrorClass | 26;
    constexpr uint16_t netErrInvalidInterface = netErrorClass | 27;
    constexpr uint16_t netErrInterfaceNotFound = netErrorClass | 28;
    constexpr uint16_t netErrTooManyInterfaces = netErrorClass | 29;
    constexpr uint16_t netErrBufWrongSize = netErrorClass | 30;
    constexpr uint16_t netErrUserCancel = netErrorClass | 31;
    constexpr uint16_t netErrBadScript = netErrorClass | 32;
    constexpr uint16_t netErrNoSocket = netErrorClass | 33;
    constexpr uint16_t netErrSocketRcvBufFull = netErrorClass | 34;
    constexpr uint16_t netErrNoPendingConnect = netErrorClass | 35;
    constexpr uint16_t netErrUnexpectedCmd = netErrorClass | 36;
    constexpr uint16_t netErrNoTCB = netErrorClass | 37;
    constexpr uint16_t netErrNilRemoteWindowSize = netErrorClass | 38;
    constexpr uint16_t netErrNoTimerProc = netErrorClass | 39;
    constexpr uint16_t netErrSocketInputShutdown = netErrorClass | 40;
    constexpr uint16_t netErrCmdBlockNotCheckedOut = netErrorClass | 41;
    constexpr uint16_t netErrCmdNotDone = netErrorClass | 42;
    constexpr uint16_t netErrUnknownProtocol = netErrorClass | 43;
    constexpr uint16_t netErrUnknownService = netErrorClass | 44;
    constexpr uint16_t netErrUnreachableDest = netErrorClass | 45;
    constexpr uint16_t netErrReadOnlySetting = netErrorClass | 46;
    constexpr uint16_t netErrWouldBlock = netErrorClass | 47;
    constexpr uint16_t netErrAlreadyInProgress = netErrorClass | 48;
    constexpr uint16_t netErrPPPTimeout = netErrorClass | 49;
    constexpr uint16_t netErrPPPBroughtDown = netErrorClass | 50;
    constexpr uint16_t netErrAuthFailure = netErrorClass | 51;
    constexpr uint16_t netErrPPPAddressRefused = netErrorClass | 52;
    constexpr uint16_t netErrDNSNameTooLong = netErrorClass | 53;
    constexpr uint16_t netErrDNSBadName = netErrorClass | 54;
    constexpr uint16_t netErrDNSBadArgs = netErrorClass | 55;
    constexpr uint16_t netErrDNSLabelTooLong = netErrorClass | 56;
    constexpr uint16_t netErrDNSAllocationFailure = netErrorClass | 57;
    constexpr uint16_t netErrDNSTimeout = netErrorClass | 58;
    constexpr uint16_t netErrDNSUnreachable = netErrorClass | 59;
    constexpr uint16_t netErrDNSFormat = netErrorClass | 60;
    constexpr uint16_t netErrDNSServerFailure = netErrorClass | 61;
    constexpr uint16_t netErrDNSNonexistantName = netErrorClass | 62;
    constexpr uint16_t netErrDNSNIY = netErrorClass | 63;
    constexpr uint16_t netErrDNSRefused = netErrorClass | 64;
    constexpr uint16_t netErrDNSImpossible = netErrorClass | 65;
    constexpr uint16_t netErrDNSNoRRS = netErrorClass | 66;
    constexpr uint16_t netErrDNSAborted = netErrorClass | 67;
    constexpr uint16_t netErrDNSBadProtocol = netErrorClass | 68;
    constexpr uint16_t netErrDNSTruncated = netErrorClass | 69;
    constexpr uint16_t netErrDNSNoRecursion = netErrorClass | 70;
    constexpr uint16_t netErrDNSIrrelevant = netErrorClass | 71;
    constexpr uint16_t netErrDNSNotInLocalCache = netErrorClass | 72;
    constexpr uint16_t netErrDNSNoPort = netErrorClass | 73;
    constexpr uint16_t netErrIPCantFragment = netErrorClass | 74;
    constexpr uint16_t netErrIPNoRoute = netErrorClass | 75;
    constexpr uint16_t netErrIPNoSrc = netErrorClass | 76;
    constexpr uint16_t netErrIPNoDst = netErrorClass | 77;
    constexpr uint16_t netErrIPktOverflow = netErrorClass | 78;
    constexpr uint16_t netErrTooManyTCPConnections = netErrorClass | 79;
    constexpr uint16_t netErrNoDNSServers = netErrorClass | 80;
    constexpr uint16_t netErrInterfaceDown = netErrorClass | 81;
    constexpr uint16_t netErrNoChannel = netErrorClass | 82;
    constexpr uint16_t netErrDieState = netErrorClass | 83;
    constexpr uint16_t netErrReturnedInMail = netErrorClass | 84;
    constexpr uint16_t netErrReturnedNoTransfer = netErrorClass | 85;
    constexpr uint16_t netErrReturnedIllegal = netErrorClass | 86;
    constexpr uint16_t netErrReturnedCongest = netErrorClass | 87;
    constexpr uint16_t netErrReturnedError = netErrorClass | 88;
    constexpr uint16_t netErrReturnedBusy = netErrorClass | 89;
    constexpr uint16_t netErrGMANState = netErrorClass | 90;
    constexpr uint16_t netErrQuitOnTxFail = netErrorClass | 91;
    constexpr uint16_t netErrFlexListFull = netErrorClass | 92;
    constexpr uint16_t netErrSenderMAN = netErrorClass | 93;
    constexpr uint16_t netErrIllegalType = netErrorClass | 94;
    constexpr uint16_t netErrIllegalState = netErrorClass | 95;
    constexpr uint16_t netErrIllegalFlags = netErrorClass | 96;
    constexpr uint16_t netErrIllegalSendlist = netErrorClass | 97;
    constexpr uint16_t netErrIllegalMPAKLength = netErrorClass | 98;
    constexpr uint16_t netErrIllegalAddressee = netErrorClass | 99;
    constexpr uint16_t netErrIllegalPacketClass = netErrorClass | 100;
    constexpr uint16_t netErrBufferLength = netErrorClass | 101;
    constexpr uint16_t netErrNiCdLowBattery = netErrorClass | 102;
    constexpr uint16_t netErrRFinterfaceFatal = netErrorClass | 103;
    constexpr uint16_t netErrIllegalLogout = netErrorClass | 104;
    constexpr uint16_t netErrAAARadioLoad = netErrorClass | 105;
    constexpr uint16_t netErrAntennaDown = netErrorClass | 106;
    constexpr uint16_t netErrNiCdCharging = netErrorClass | 107;
    constexpr uint16_t netErrAntennaWentDown = netErrorClass | 108;
    constexpr uint16_t netErrNotActivated = netErrorClass | 109;
    constexpr uint16_t netErrRadioTemp = netErrorClass | 110;
    constexpr uint16_t netErrNiCdChargeError = netErrorClass | 111;
    constexpr uint16_t netErrNiCdSag = netErrorClass | 112;
    constexpr uint16_t netErrNiCdChargeSuspend = netErrorClass | 113;
    constexpr uint16_t netErrConfigNotFound = netErrorClass | 115;
    constexpr uint16_t netErrConfigCantDelete = netErrorClass | 116;
    constexpr uint16_t netErrConfigTooMany = netErrorClass | 117;
    constexpr uint16_t netErrConfigBadName = netErrorClass | 118;
    constexpr uint16_t netErrConfigNotAlias = netErrorClass | 119;
    constexpr uint16_t netErrConfigCantPointToAlias = netErrorClass | 120;
    constexpr uint16_t netErrConfigEmpty = netErrorClass | 121;
    constexpr uint16_t netErrAlreadyOpenWithOtherConfig = netErrorClass | 122;
    constexpr uint16_t netErrConfigAliasErr = netErrorClass | 123;
    constexpr uint16_t netErrNoMultiPktAddr = netErrorClass | 124;
    constexpr uint16_t netErrOutOfPackets = netErrorClass | 125;
    constexpr uint16_t netErrMultiPktAddrReset = netErrorClass | 126;
    constexpr uint16_t netErrStaleMultiPktAddr = netErrorClass | 127;
    constexpr uint16_t netErrScptPluginMissing = netErrorClass | 128;
    constexpr uint16_t netErrScptPluginLaunchFail = netErrorClass | 129;
    constexpr uint16_t netErrScptPluginCmdFail = netErrorClass | 130;
    constexpr uint16_t netErrScptPluginInvalidCmd = netErrorClass | 131;

    uint16_t errnoToPalm(int err);
}  // namespace NetworkCodes

#endif  //  __NETWORK_CODES_H_