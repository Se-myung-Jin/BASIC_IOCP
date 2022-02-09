#include "IOCompletionPort.h"
#include <string>
#include <iostream>

const UINT16 SERVER_PORT = 8080;
const UINT16 MAX_CLIENT = 200;

int main()
{
	IOCompletionPort iocp;

	iocp.InitSocket();

	iocp.ListenSocket(SERVER_PORT);

	iocp.StartServer(MAX_CLIENT);

	getchar();

	iocp.EndServer();

	return 0;
}