#pragma once
#pragma comment(lib, "ws2_32")

#include "Define.h"
#include <thread>
#include <vector>

class IOCPServer
{
public:
	IOCPServer(void)
	{

	}

	~IOCPServer(void)
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

	virtual void OnConnect(const UINT32 _clientIndex) {}

	virtual void OnClose(const UINT32 _clientIndex) {}

	virtual void OnReceive(const UINT32 _clientIndex, const UINT32 _size, char* _pData) {}


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

			pClientInfo->Client = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (INVALID_SOCKET == pClientInfo->Client)
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
			printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->Client);

			OnConnect(pClientInfo->Index);

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
				printf("socket(%d) disconnected\n", (int)pClientInfo->Client);
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			if (IOOperation::RECV == pOverlappedEx->Operation)
			{
				OnReceive(pClientInfo->Index, dwIOSize, pClientInfo->RecvBuf);

				SendMsg(pClientInfo, pClientInfo->RecvBuf, dwIOSize);

				BindRecv(pClientInfo);
			}
			else if (IOOperation::SEND == pOverlappedEx->Operation)
			{
				printf("[송신] bytes : %d , msg : %s\n", dwIOSize, pClientInfo->SendBuf);
			}
		}
	}

	stClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (INVALID_SOCKET == client.Client)
			{
				return &client;
			}
		}

		return nullptr;
	}

	bool BindIOCompletionPort(stClientInfo* _pClientInfo)
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)_pClientInfo->Client, mIOCPHandle, (ULONG_PTR)(_pClientInfo), 0);

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

		_pClientInfo->RecvOverlappedEx.Buf.len = MAX_SOCKBUF;
		_pClientInfo->RecvOverlappedEx.Buf.buf = _pClientInfo->RecvBuf;
		_pClientInfo->RecvOverlappedEx.Operation = IOOperation::RECV;

		int nRet = WSARecv(_pClientInfo->Client, &(_pClientInfo->RecvOverlappedEx.Buf), 1, &dwRecvNumBytes, &dwFlag, (LPWSAOVERLAPPED)&(_pClientInfo->RecvOverlappedEx), NULL);
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

		CopyMemory(_pClientInfo->SendBuf, _pMsg, _nLen);

		_pClientInfo->SendOverlappedEx.Buf.buf = _pClientInfo->SendBuf;
		_pClientInfo->SendOverlappedEx.Buf.len = _nLen;
		_pClientInfo->SendOverlappedEx.Operation = IOOperation::SEND;

		int nRet = WSASend(_pClientInfo->Client, &(_pClientInfo->SendOverlappedEx.Buf), 1, &dwSendNumBytes, 0, (LPWSAOVERLAPPED)&(_pClientInfo->SendOverlappedEx), NULL);
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	void CloseSocket(stClientInfo* _pClientInfo, bool _bIsForce = false)
	{
		auto clientIndex = _pClientInfo->Index;

		struct linger stLinger = { 0, 0 };

		if (true == _bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		shutdown(_pClientInfo->Client, SD_BOTH);

		setsockopt(_pClientInfo->Client, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(_pClientInfo->Client);

		_pClientInfo->Client = INVALID_SOCKET;

		OnClose(clientIndex);
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