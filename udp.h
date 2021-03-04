#pragma once
#include <iostream>
#include <winsock2.h>
#pragma warning(disable:4996)
namespace udp {
	class udpserver {
	};
	class udpclient {
	private:
		struct sockaddr_in si_other;
		int s, slen;
		//char buf[512];
		//char message[512];
		WSADATA wsa;
		std::string SERVER;
		int PORT;
	public:
		udpclient(int PORT);
		~udpclient();

		//return udp target ip address
		std::string ip();

		//return udp target port
		int port();

		//send char array to target
		void send(char* message);


	};
}
