#pragma once
#include <iostream>
#include <winsock2.h>
#include <sstream>
//#include <ppltasks.h>
#pragma warning(disable:4996)
namespace udp {
	class udpserver {

	};
	class udpclient {
	private:
		unsigned short myport;
		int ssrc = 0;
		struct sockaddr_in si_other;
		int s, slen = sizeof(si_other);
		//char buf[512];
		//char message[512];
		WSADATA wsa;
		std::string serverIP = ""; //ip address of udp server
		int serverPORT = 0; //The port on which they listen for incoming data
		std::string clientIP = ""; 
		int clientPORT = 0;
	public:
		//udpclient(int serverPORT,std::string serverIP) {
		//	this->serverIP = serverIP;
		//	this->serverPORT = serverPORT;
		//	//Init
		//	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		//	{
		//		std::cout << "Failed. Error Code : %d" << WSAGetLastError() << std::endl;
		//		exit(EXIT_FAILURE);
		//	}
		//	std::cout << ("Initialized.\n");
		//	//end init

		//	//create socket
		//	memset((char*)&si_other, 0, sizeof(si_other));
		//	si_other.sin_family = AF_INET;
		//	si_other.sin_port = htons(serverPORT);
		//	si_other.sin_addr.S_un.S_addr = inet_addr(serverIP.c_str());
		//	//end
		//	return;
		//}
		////manual set udp client attributes
		//udpclient() {
		//	return;
		//}

		~udpclient() {
			closesocket(s);
			WSACleanup();
		}
		
		void cleanup() {
			closesocket(s);
			WSACleanup();
		}
		void renew(int serverPORT, std::string serverIP) {
			closesocket(s);
			WSACleanup();
			start(serverPORT,serverIP);
		}

		void setssrc(int ssrc) {
			this->ssrc = ssrc;
			std::cout << "Get ssrc: " << ssrc << std::endl;
		}

		void start(int serverPORT, std::string serverIP) {
			this->serverIP = serverIP;
			this->serverPORT = serverPORT;
			std::cout << "Attemp to start udp client to: " << this->serverIP << ":" << this->serverPORT << std::endl;
			//Init
			if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			{
				std::cout << "Failed. Error Code : %d" << WSAGetLastError() << std::endl;
				exit(EXIT_FAILURE);
			}
			std::cout << ("Initialized.\n");
			//end init

			//create socket
			if ((this->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
			{
				printf("socket() failed with error code : %d", WSAGetLastError());
				exit(EXIT_FAILURE);
			}
			//setup address structure
			memset((char*)&this->si_other, 0, sizeof(this->si_other));
			si_other.sin_family = AF_INET;
			si_other.sin_port = htons(this->serverPORT);
			si_other.sin_addr.S_un.S_addr = inet_addr(this->serverIP.c_str());
			std::cout << "Created socket\n";
			//end
			return;
		}

		void start() {
			if (serverPORT == 0 || serverIP == "") {
				std::cout << "You must set Port and Ip first";
			}
			else {
				return;
			}
		}
		//return udp server ip address
		std::string ip() {
			return serverIP;
		}

		//return client udp port
		int port() {
			return serverPORT;
		}

		//send char array to target
		int send(char* message) {
			if (sendto(s, message, strlen(message), 0, (struct sockaddr*)&si_other, slen) == SOCKET_ERROR)
			{
				printf("sendto() failed with error code : %d", WSAGetLastError());
				//exit(EXIT_FAILURE);
				return WSAGetLastError();
			}
			return 0;
		}

		int send(std::string message) {
			if (sendto(s, message.c_str(), message.length(), 0, (struct sockaddr*)&si_other, slen) == SOCKET_ERROR)
			{
				printf("sendto() failed with error code : %d", WSAGetLastError());
				//exit(EXIT_FAILURE);
				return WSAGetLastError();
			}
			return 0;
		}
		
		std::string clientip() {
			return clientIP;
		}

		int clientport() {
			return clientPORT;
		}

		//Cautions: blocking call
		char* get() {
			char buf[1000];
			memset(buf, '\0', 1000);
			if (recvfrom(s, buf, 1000, 0, (struct sockaddr*)&si_other, &slen) == SOCKET_ERROR)
			{
				printf("recvfrom() failed with error code : %d", WSAGetLastError());
				exit(EXIT_FAILURE);
			}
			return buf;
		}

		void ipDiscovery() {
			std::cout << "Perform IP discovery\n";
			char a[74];
			for (int i = 0; i < 74; i++) {
				a[i] = '\0';
			}
			std::string msg;
			msg.resize(74, '\0');
			// Type: 0x1 for request, 0x2 for response
			msg[0] = 0x1 >> 8;
			msg[1] = 0x1;
			// Messmsgge length excluding Type msgnd Length fields (msglwmsgys 70)
			msg[2] = 70 >> 8;
			msg[3] = 70;
			// SSRC
			msg[4] = ssrc >> 24;
			msg[5] = ssrc >> 16;
			msg[6] = ssrc >> 8;
			msg[7] = ssrc;
			send(msg);
			char* recv_msg = get();

			unsigned char c_ip[64];
			for (int i = 8; i < 72; i++) {
				c_ip[i - 8] = recv_msg[i];
			}
			//char c_port[2];
			//c_port[0] = recv_msg[72];
			//c_port[1] = recv_msg[73];
			//std::string::copy(recv_msg, 8, 0);
			//std::string my_ip = recv_msg.substr(8, 20);
			//for (unsigned int i = 0; i < my_ip.size(); i++) {
			//	if (my_ip[i] == 0) {
			//		my_ip.resize(i, '\0');
			//		break;
			//	}
			//}
			//unsigned short my_port = (unsigned short)strtoul(c_port, NULL, 0);
			//std::cout << "=================="<<(int)recv_msg[72] << (int)recv_msg[73];
			unsigned short my_port = (recv_msg[72] << 8) | (recv_msg[73]);
			//my_port = (my_port >> 8) | (my_port << 8);
			std::string ip = std::string(reinterpret_cast<char*>(c_ip));
			std::cout << "Client IP: " << ip << std::endl;
			std::cout << "Client Port: " <<  my_port << std::endl;
			std::cout << "Verify Port: " << (int)my_port << std::endl;
			clientIP = ip;
			clientPORT = (int)my_port;
			return;
		}
	};
}
