#pragma once
#pragma comment(lib, "ws2_32")

#include "ClientInfo.h"
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

	bool SendMsg(const UINT32 _sessionIndex, const UINT32 _dataSize, char* _pData)
	{
		auto pClient = GetClientInfo(_sessionIndex);
		return pClient->SendMsg(_dataSize, _pData);
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

			mClientInfos[i].Init(i);
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
			CClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo)
			{
				printf("[에러] client full\n");
				return;
			}

			auto newSocket = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (INVALID_SOCKET == newSocket)
			{
				continue;
			}

			if (pClientInfo->OnConnect(mIOCPHandle, newSocket) == false)
			{
				pClientInfo->Close(true);
				return;
			}

			char clientIP[32] = { 0, };
			inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)newSocket);

			OnConnect(pClientInfo->GetIndex());

			++mClientCnt;
		}
	}

	void WorkerThread()
	{
		CClientInfo* pClientInfo = NULL;
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
				printf("socket(%d) disconnected\n", (int)pClientInfo->GetSock());
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			if (IOOperation::RECV == pOverlappedEx->Operation)
			{
				OnReceive(pClientInfo->GetIndex(), dwIOSize, pClientInfo->RecvBuffer());

				pClientInfo->BindRecv();
			}
			else if (IOOperation::SEND == pOverlappedEx->Operation)
			{
				delete[] pOverlappedEx->Buf.buf;
				delete pOverlappedEx;
				pClientInfo->SendCompleted(dwIOSize);
			}
		}
	}

	CClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (INVALID_SOCKET == client.GetSock())
			{
				return &client;
			}
		}

		return nullptr;
	}

	CClientInfo* GetClientInfo(const UINT32 sessionIndex)
	{
		return &mClientInfos[sessionIndex];
	}

	void CloseSocket(CClientInfo* _pClientInfo, bool _bIsForce = false)
	{
		auto clientIndex = _pClientInfo->GetIndex();

		_pClientInfo->Close(_bIsForce);

		OnClose(clientIndex);
	}


private:
	SOCKET							mListenSocket = INVALID_SOCKET;

	HANDLE							mIOCPHandle = INVALID_HANDLE_VALUE;

	bool							mIsWorkerRun = true;

	bool							mIsAccepterRun = true;

	std::vector<std::thread>		mIOWorkerThreads;

	std::thread						mAccepterThread;

	std::vector<CClientInfo>		mClientInfos;

	int								mClientCnt = 0;
};