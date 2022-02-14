#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

const UINT32 MAX_SOCKBUF = 256;
const UINT32 MAX_WORKERTHREAD = 4;

enum class IOOperation
{
	RECV,
	SEND
};

struct stOverlappedEx
{
	WSAOVERLAPPED		Overlapped;
	SOCKET				Client;
	WSABUF				Buf;
	IOOperation			Operation;
};

struct stClientInfo
{
	INT32				Index = 0;
	SOCKET				Client;
	stOverlappedEx		RecvOverlappedEx;
	stOverlappedEx		SendOverlappedEx;

	char RecvBuf[MAX_SOCKBUF];
	char SendBuf[MAX_SOCKBUF];

	stClientInfo()
	{
		ZeroMemory(&RecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&SendOverlappedEx, sizeof(stOverlappedEx));
		Client = INVALID_SOCKET;
	}
};