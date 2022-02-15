#pragma once

#include "Define.h"
#include <stdio.h>

class CClientInfo
{
public:
	CClientInfo()
	{
		ZeroMemory(&RecvOverlappedEx, sizeof(stOverlappedEx));
		Socket = INVALID_SOCKET;
	}

	void Init(const UINT32 _index)
	{
		Index = _index;
	}

	UINT32 GetIndex() { return Index; }

	bool IsConnectd() { return Socket != INVALID_SOCKET; }

	SOCKET GetSock() { return Socket; }

	char* RecvBuffer() { return RecvBuf; }

	bool OnConnect(HANDLE _iocpHandle, SOCKET _socket)
	{
		Socket = _socket;

		Clear();

		if (BindIOCompletionPort(_iocpHandle) == false)
		{
			return false;
		}

		return BindRecv();
	}

	void Close(bool _bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };

		if (true == _bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		shutdown(Socket, SD_BOTH);

		setsockopt(Socket, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(Socket);
		Socket = INVALID_SOCKET;
	}

	void Clear() {}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		RecvOverlappedEx.Buf.len = MAX_SOCKBUF;
		RecvOverlappedEx.Buf.buf = RecvBuf;
		RecvOverlappedEx.Operation = IOOperation::RECV;

		int nRet = WSARecv(Socket,
			&(RecvOverlappedEx.Buf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (RecvOverlappedEx),
			NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSARecv : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool SendMsg(const UINT32 _dataSize, char* _pMsg)
	{
		auto sendOverlappedEx = new stOverlappedEx;
		ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));
		
		sendOverlappedEx->Buf.len = _dataSize;
		sendOverlappedEx->Buf.buf = new char[_dataSize];
		CopyMemory(sendOverlappedEx->Buf.buf, _pMsg, _dataSize);
		sendOverlappedEx->Operation = IOOperation::SEND;

		DWORD dwSendNumBytes = 0;
		int nRet = WSASend(Socket,
			&(sendOverlappedEx->Buf),
			1,
			&dwSendNumBytes,
			0,
			(LPWSAOVERLAPPED)sendOverlappedEx,
			NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool BindIOCompletionPort(HANDLE _iocpHandle)
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSock()
			, _iocpHandle
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[에러] CreateIoCompletionPort : %d\n", GetLastError());
			return false;
		}

		return true;
	}

	void SendCompleted(const UINT32 _dataSize)
	{
		printf("[송신 완료] bytes : %d\n", _dataSize);
	}


private:
	INT32			Index = 0;
	SOCKET			Socket;
	stOverlappedEx	RecvOverlappedEx;

	char			RecvBuf[MAX_SOCKBUF];
};