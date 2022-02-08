#pragma once
#pragma comment(lib, "ws2_32") // ws2_32.lib 파일 필요
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <thread>
#include <vector>

#define MAX_SOCKBUF 1024
#define MAX_WORKERTHREAD 4

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
		// Winsock 종료
		WSACleanup();
	}

	// Socket init func
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (0 != nRet)
		{
			printf("[에러] WSAStartup : %d\n", WSAGetLastError());
			return false;
		}

		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
		if (INVALID_SOCKET == mListenSocket)
		{
			printf("[에러] InitSocket : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// Socket listen func
	bool ListenSocket(int _port)
	{
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(_port);
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[에러] bind : %d\n", WSAGetLastError());
			return false;
		}

		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[에러] listen : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// Start server
	bool StartServer(const UINT32 _maxClientCnt)
	{
		CreateClient(_maxClientCnt);

		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle)
		{
			printf("[에러] CreateIoCompletionPort : %d\n", GetLastError());
			return false;
		}

		bool bRet = CreateWorkerThread();
		if (false == bRet)
		{
			return false;
		}

		bRet = CreateAccepterThread();
		if (false == bRet)
		{
			return false;
		}

		return true;
	}

	// End server
	void EndServer()
	{
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);

		for (auto& thr : mIOWorkerThreads)
		{
			if (thr.joinable())
			{
				thr.join();
			}
		}

		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
	}


private:
	void CreateClient(const UINT32 _maxClientCnt)
	{
		for (UINT32 i = 0; i < _maxClientCnt; ++i)
		{
			mClientInfos.emplace_back();
		}
	}

	bool CreateWorkerThread()
	{
		unsigned int uiThreadID = 0;
		for (int i = 0; i < MAX_WORKERTHREAD; ++i)
		{
			mIOWorkerThreads.emplace_back([this]() {WorkerThread(); });
		}

		return true;
	}

	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() {AccepterThread(); });

		return true;
	}

	void AccepterThread()
	{
		SOCKADDR_IN stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			stClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo)
			{
				printf("[에러] client full\n");
				return;
			}

			pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (INVALID_SOCKET == pClientInfo->m_socketClient)
			{
				continue;
			}

			bool bRet = BindIOCompletionPort(pClientInfo);
			if (false == bRet)
			{
				return;
			}

			bRet = BindRecv(pClientInfo);
			if (false == bRet)
			{
				return;
			}

			char clientIP[32] = { 0, };
			inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->m_socketClient);

			++mClientCnt;
		}
	}

	void WorkerThread()
	{
		stClientInfo* pClientInfo = NULL;
		BOOL bSuccess = TRUE;
		DWORD dwIOSize = 0;
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			bSuccess = GetQueuedCompletionStatus(mIOCPHandle, &dwIOSize, (PULONG_PTR)&pClientInfo, &lpOverlapped, INFINITE);
			// 사용자 쓰레드 종료
			if (TRUE == bSuccess && 0 == dwIOSize && NULL == lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
			{
				continue;
			}

			// 클라이언트 접속 종료
			if (FALSE == bSuccess || (0 == dwIOSize && TRUE == bSuccess))
			{
				printf("socket(%d) disconnected\n", (int)pClientInfo->m_socketClient);
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				pOverlappedEx->m_szBuf[dwIOSize] = NULL;

				SendMsg(pClientInfo, pOverlappedEx->m_szBuf, dwIOSize);

				BindRecv(pClientInfo);
			}
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				printf("[송신] bytes : %d , msg : %s\n", dwIOSize, pOverlappedEx->m_szBuf);
			}
		}
	}

	stClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (INVALID_SOCKET == client.m_socketClient)
			{
				return &client;
			}
		}

		return nullptr;
	}

	bool BindIOCompletionPort(stClientInfo* _pClientInfo)
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)_pClientInfo->m_socketClient, mIOCPHandle, (ULONG_PTR)(_pClientInfo), 0);

		if (NULL == hIOCP || mIOCPHandle != hIOCP)
		{
			printf("[에러] BindIOCompletionPort : %d\n", GetLastError());
			return false;
		}

		return true;
	}

	bool BindRecv(stClientInfo* _pClientInfo)
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		_pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		_pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.buf = _pClientInfo->m_stRecvOverlappedEx.m_szBuf;
		_pClientInfo->m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(_pClientInfo->m_socketClient, &(_pClientInfo->m_stRecvOverlappedEx.m_wsaBuf), 1, &dwRecvNumBytes, &dwFlag, (LPWSAOVERLAPPED)&(_pClientInfo->m_stRecvOverlappedEx), NULL);
		if (SOCKET_ERROR == nRet && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSARecv : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool SendMsg(stClientInfo* _pClientInfo, char* _pMsg, int _nLen)
	{
		DWORD dwSendNumBytes = 0;

		CopyMemory(_pClientInfo->m_stSendOverlappedEx.m_szBuf, _pMsg, _nLen);

		_pClientInfo->m_stSendOverlappedEx.m_wsaBuf.buf = _pClientInfo->m_stSendOverlappedEx.m_szBuf;
		_pClientInfo->m_stSendOverlappedEx.m_wsaBuf.len = _nLen;
		_pClientInfo->m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(_pClientInfo->m_socketClient, &(_pClientInfo->m_stSendOverlappedEx.m_wsaBuf), 1, &dwSendNumBytes, 0, (LPWSAOVERLAPPED)&(_pClientInfo->m_stSendOverlappedEx), NULL);
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	void CloseSocket(stClientInfo* _pClientInfo, bool _bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };

		if (true == _bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		shutdown(_pClientInfo->m_socketClient, SD_BOTH);

		setsockopt(_pClientInfo->m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(_pClientInfo->m_socketClient);

		_pClientInfo->m_socketClient = INVALID_SOCKET;
	}


private:
	SOCKET							mListenSocket = INVALID_SOCKET;

	HANDLE							mIOCPHandle = INVALID_HANDLE_VALUE;

	bool							mIsWorkerRun = true;

	bool							mIsAccepterRun = true;

	std::vector<std::thread>		mIOWorkerThreads;

	std::thread						mAccepterThread;

	std::vector<stClientInfo>		mClientInfos;

	int								mClientCnt = 0;
};