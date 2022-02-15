#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct PacketData
{
	UINT32 SessionIndex = 0;
	UINT32 DataSize = 0;
	char* pPacketData = nullptr;

	void Set(PacketData& value)
	{
		SessionIndex = value.SessionIndex;
		DataSize = value.DataSize;

		pPacketData = new char[value.DataSize];
		CopyMemory(pPacketData, value.pPacketData, value.DataSize);
	}

	void Set(UINT32 sessionIndex, UINT32 dataSize, char* pData)
	{
		SessionIndex = sessionIndex;
		DataSize = dataSize;

		CopyMemory(pPacketData, pData, dataSize);
	}

	void Release()
	{
		delete pPacketData;
	}
};