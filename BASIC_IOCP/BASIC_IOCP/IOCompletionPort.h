#pragma once
#pragma comment(lib, "ws2_32") // ws2_32.lib 파일 필요
#include <WinSock2.h>
#include <WS2tcpip.h>

#define MAX_SOCKBUF 1024

enum class IOOperation
{
	RECV,
	SEND
};

struct stOverlappedEx
{
	WSAOVERLAPPED m_wsaOverlapped;
	SOCKET m_socketClient;
	WSABUF m_wsaBuf;				//Overlapped I/O 작업 버퍼
	char m_szBuf[MAX_SOCKBUF];		//데이터 버퍼
	IOOperation m_eOperation;
};

struct stClientInfo
{
	SOCKET m_socketClient;
	stOverlappedEx m_stRecvOverlappedEx;
	stOverlappedEx m_stSendOverlappedEx;

	stClientInfo()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
};

class IOCompletionPort
{
public:
	IOCompletionPort(void)
	{

	}

	~IOCompletionPort(void)
	{

	}
};