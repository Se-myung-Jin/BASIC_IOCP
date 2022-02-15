#include "EchoServer.h"
#include <string>
#include <iostream>

const UINT16 SERVER_PORT = 8080;
const UINT16 MAX_CLIENT = 200;

int main()
{
	EchoServer server;

	server.InitSocket();

	server.ListenSocket(SERVER_PORT);

	server.Run(MAX_CLIENT);

	getchar();

	server.End();
	return 0;
}