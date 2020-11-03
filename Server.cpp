
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")

#include <winsock2.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <time.h>


const int PORT = 8080;
const int MAX_SOCKETS = 60;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;

const int GET = 1;
const int HEAD = 2;
const int PUT = 3;
const int _DELETE = 4;
const int OPTIONS = 5;
const int TRACE = 6;
const int INVALID_REQUEST = 7;

const int MAX_SIZE = 500;
const int PATH_SIZE = 48;
const int TIMEOUT = 120;

const char* s_STATUS_CODE_200 = "HTTP/1.1 200 OK\r\n";
const char* s_STATUS_CODE_201 = "HTTP / 1.1 201 Created\r\n";
const char* s_STATUS_CODE_204 = "HTTP/1.1 204 No Content\r\n";
const char* s_STATUS_CODE_404 = "HTTP/1.1 404 Not Found\r\n";
const char* s_STATUS_CODE_500 = "HTTP/1.1 500 Internal Server Error\r\n";
const char* s_FILE_DELETED_SUCCESSFULLY = "File deleted successfully";


struct SocketState
{
	SOCKET id;			// Socket handle
	int	recv;			// Receiving?
	int	send;			// Sending?
	int sendSubType;	// Sending sub-type
	char buffer[MAX_SIZE];
	int len;
	time_t timeFirstSeen;
};

SOCKET setupSocket();

bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);

char * getDateTime();


void updateSocketByReceivedMessage(int index, int i_Type, const char* i_RequestName);

void doGET(int index, char* sendBuff);
void doHEAD(int index, char* buffer);
void doPUT(int index, char* buffer);
void doOPTIONS(int index, char* sendBuff);
void doTRACE(int index, char* sendBuff);
void doDELETE(int index, char* sendBuff);

void getValidPath(int index, char* i_Path);
void getFileContent(int index, char* i_Content);


string find_start_of_body(char *header);


///////////////////

struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;


void main()
{
	// Initialize Winsock (Windows Sockets).

	// Create a WSADATA object called wsaData.
	// The WSADATA structure contains information about the Windows 
	// Sockets implementation.
	WSAData wsaData;

	// Call WSAStartup and return its value as an integer and check for errors.
	// The WSAStartup function initiates the use of WS2_32.DLL by a process.
	// First parameter is the version number 2.2.
	// The WSACleanup function destructs the use of WS2_32.DLL by a process.
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Web Server: Error at WSAStartup()\n";
		return;
	}

	SOCKET listenSocket = setupSocket();
	if (listenSocket == ERROR)
	{
		cout << "Web Server: Error at setupSocket()\n";
		return; 
	}
	   	 	

	// Listen on the Socket for incoming connections.
	// This socket accepts only one connection (no pending connections 
	// from other clients). This sets the backlog parameter.
	if (SOCKET_ERROR == listen(listenSocket, 5))
	{
		cout << "Web Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
	addSocket(listenSocket, LISTEN);

	// Accept connections and handles them one by one.
	while (true)
	{
		// The select function determines the status of one or more sockets,
		// waiting if necessary, to perform asynchronous I/O. Use fd_sets for
		// sets of handles for reading, writing and exceptions. select gets "timeout" for waiting
		// and still performing other operations (Use NULL for blocking). Finally,
		// select returns the number of descriptors which are ready for use (use FD_ISSET
		// macro to check which descriptor in each set is ready to be used).
		fd_set waitRecv;
		FD_ZERO(&waitRecv);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
				FD_SET(sockets[i].id, &waitRecv);
		}

		fd_set waitSend;
		FD_ZERO(&waitSend);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].send == SEND)
				FD_SET(sockets[i].id, &waitSend);
		}

		//
		// Wait for interesting event.
		// Note: First argument is ignored. The fourth is for exceptions.
		// And as written above the last is a timeout, hence we are blocked if nothing happens.
		//

		timeval timeout;

		int nfd;
		nfd = select(0, &waitRecv, &waitSend, NULL, &timeout);
		if (nfd == SOCKET_ERROR)
		{
			cout << "Web Server: Error at select(): " << WSAGetLastError() << endl;
			WSACleanup();
			return;
		}

		time_t currTime;
		time(&currTime);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].recv != EMPTY && sockets[i].recv != LISTEN && currTime - sockets[i].timeFirstSeen > TIMEOUT)
			{
				cout << "Web Server: Socket " << i << " has timed out." << endl;
				removeSocket(i);
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitRecv))
			{
				nfd--;
				switch (sockets[i].recv)
				{
				case LISTEN:
					acceptConnection(i);
					break;

				case RECEIVE:
					receiveMessage(i);
					break;
				}
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				switch (sockets[i].send)
				{
				case SEND:
					sendMessage(i);
					break;
				}
			}
		}
	}

	// Closing connections and Winsock.
	cout << "Web Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}


SOCKET setupSocket()
{
	// Server side:
	// Create and bind a socket to an internet address.
	// Listen through the socket for incoming connections.

	// After initialization, a SOCKET object is ready to be instantiated.

	// Create a SOCKET object called listenSocket. 
	// For this application:	use the Internet address family (AF_INET), 
	//							streaming sockets (SOCK_STREAM), 
	//							and the TCP/IP protocol (IPPROTO_TCP).
	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Check for errors to ensure that the socket is a valid socket.
	// Error detection is a key part of successful networking code. 
	// If the socket call fails, it returns INVALID_SOCKET. 
	// The if statement in the previous code is used to catch any errors that
	// may have occurred while creating the socket. WSAGetLastError returns an 
	// error number associated with the last error that occurred.
	if (INVALID_SOCKET == listenSocket)
	{
		cout << "Web Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return ERROR;
	}

	// For a server to communicate on a network, it must bind the socket to 
	// a network address.

	// Need to assemble the required data for connection in sockaddr structure.

	// Create a sockaddr_in object called serverService. 
	sockaddr_in serverService;
	// Address family (must be AF_INET - Internet address family).
	serverService.sin_family = AF_INET;
	// IP address. The sin_addr is a union (s_addr is a unsigned long 
	// (4 bytes) data type).
	// inet_addr (Iternet address) is used to convert a string (char *) 
	// into unsigned long.
	// The IP address is INADDR_ANY to accept connections on all interfaces.
	serverService.sin_addr.s_addr = INADDR_ANY;
	// IP Port. The htons (host to network - short) function converts an
	// unsigned short from host to TCP/IP network byte order 
	// (which is big-endian).
	serverService.sin_port = htons(PORT);

	// Bind the socket for client's requests.

	// The bind function establishes a connection to a specified socket.
	// The function uses the socket handler, the sockaddr structure (which
	// defines properties of the desired connection) and the length of the
	// sockaddr structure (in bytes).
	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR *)&serverService, sizeof(serverService)))
	{
		cout << "Web Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return ERROR;
	}

	return listenSocket;
}


bool addSocket(SOCKET id, int what)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
			socketsCount++;
			time(&sockets[i].timeFirstSeen);
			return (true);
		}
	}
	return (false);
}

void removeSocket(int index)
{
	sockets[index].recv = EMPTY;
	sockets[index].send = EMPTY;
	socketsCount--;
}

void acceptConnection(int index)
{
	SOCKET id = sockets[index].id;
	struct sockaddr_in from;		// Address of sending partner
	int fromLen = sizeof(from);

	SOCKET msgSocket = accept(id, (struct sockaddr *)&from, &fromLen);
	if (INVALID_SOCKET == msgSocket)
	{
		cout << "Web Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "Web Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

	
	// Set the socket to be in non-blocking mode
	unsigned long flag = 1;
	if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0)
	{
		cout << "Web Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	if (addSocket(msgSocket, RECEIVE) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(id);
	}
	return;
}

void receiveMessage(int index)
{
	SOCKET msgSocket = sockets[index].id;

	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv)
	{
		cout << "Web Server: Error at recv(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	if (bytesRecv == 0)
	{
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	else
	{
		sockets[index].buffer[len + bytesRecv] = '\0'; //add the null-terminating to make it a string
		cout << "Web Server: Recieved: " << bytesRecv << " bytes of the request \n\"" << &sockets[index].buffer[0] << "\".\n\n";

		sockets[index].len += bytesRecv;
		time(&sockets[index].timeFirstSeen);
		sockets[index].send = SEND;

		if (strncmp(sockets[index].buffer, "GET", 3) == 0)
		{
			updateSocketByReceivedMessage(index, GET, "GET");
		}
		else if (strncmp(sockets[index].buffer, "HEAD", 4) == 0)
		{
			updateSocketByReceivedMessage(index, HEAD, "HEAD");
		}
		else if (strncmp(sockets[index].buffer, "PUT", 3) == 0)
		{
			updateSocketByReceivedMessage(index, PUT, "PUT");
		}
		else if (strncmp(sockets[index].buffer, "DELETE", 6) == 0)
		{
			updateSocketByReceivedMessage(index, _DELETE, "DELETE");
		}
		else if (strncmp(sockets[index].buffer, "TRACE", 5) == 0)
		{
			updateSocketByReceivedMessage(index, TRACE, "TRACE");
		}
		
		else if (strncmp(sockets[index].buffer, "OPTIONS", 7) == 0)
		{
			updateSocketByReceivedMessage(index, OPTIONS, "OPTIONS");
		}
		else if (strncmp(sockets[index].buffer, "EXIT", 4) == 0)
		{
			closesocket(msgSocket);
			removeSocket(index);
			return;
		}
		else
		{
			sockets[index].send = SEND;
			sockets[index].sendSubType = INVALID_REQUEST;
			return;
		}
	}

}


void updateSocketByReceivedMessage(int index, int SubTypeNumber, const char* MethodName)
{
	int nameLength = strlen(MethodName);
	sockets[index].sendSubType = SubTypeNumber;
	sockets[index].len -= nameLength;
	memcpy(sockets[index].buffer, &sockets[index].buffer[nameLength], sockets[index].len);
}


void sendMessage(int index)
{
	int bytesSent = 0;
	char sendBuff[MAX_SIZE];

	int mtype = sockets[index].sendSubType;
	SOCKET msgSocket = sockets[index].id;

	if (mtype == GET)
	{
		doGET(index, sendBuff);
	}

	else if (mtype == HEAD)
	{
		doHEAD(index, sendBuff);
	}
	  	
	else if (mtype == PUT)
	{
		doPUT(index, sendBuff);
	}
	else if (mtype == TRACE)
	{
		doTRACE(index, sendBuff);
	}
	else if (mtype == _DELETE)
	{
		doDELETE(index, sendBuff);
	}
	else if (mtype == OPTIONS)
	{
		doOPTIONS(index, sendBuff);
	}
		   
	bytesSent = send(msgSocket, sendBuff, (int)strlen(sendBuff), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Web Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Web Server: Sent: " << bytesSent << "\\" << strlen(sendBuff) << " bytes of \n\"" << sendBuff << "\" message.\n\n";

	sockets[index].send = IDLE;

	sockets[index].len = 0;
}

void getValidPath(int index, char* i_Path)
{
	char temp[MAX_SIZE];

	strcpy(temp, sockets[index].buffer);

	for (int i = 0; i < strlen(temp); i++)
	{
		if (i_Path[i] == '/')
		{
			i_Path[i] = '\\';
		}
	}

	strcpy(i_Path, strtok(temp, " "));
	strcpy(i_Path, i_Path + 1); 
}


void getFileContent(int index, char* i_Content)
{
	char temp[MAX_SIZE];
	char* pointer;

	sockets[index].buffer[sockets[index].len] = '\0';
	strcpy(temp, sockets[index].buffer);
	pointer = strchr(sockets[index].buffer, '\r');
	pointer++;
	strcpy(i_Content, pointer);
}


void doOPTIONS(int index, char* sendBuff)
{
	char* response = new char[MAX_SIZE];
	char* contentLength = new char[3];

	strcpy(response, s_STATUS_CODE_204);
	
	strcat(response, "\r\nDate: ");
	char* curDate = getDateTime();
	strcat(response, curDate);
	
	strcat(response, "\r\nAllow: GET, HEAD, PUT, TRACE, DELETE, OPTIONS");
	strcat(response, "\r\n\r\n");

	strcpy(sendBuff, response);
}


void doGET(int index, char* sendBuff)
{
	FILE* filePtr = NULL;
	char* fileName;
	char strFileSize[50];
	char tmpBuffer[MAX_SIZE];
	int fileSize = 0;

	fileName = sockets[index].buffer;
	fileName = strtok(fileName, " ");
	memmove(fileName, fileName + 1, strlen(fileName));

	if (fileName != NULL)
	{
		filePtr = fopen(fileName, "r");
	}

	if (filePtr == NULL)
	{
		strcpy(sendBuff, s_STATUS_CODE_404);
	}
	else
	{
		//get size of file
		fseek(filePtr, 0, SEEK_END);
		fileSize = ftell(filePtr);
		fseek(filePtr, 0, SEEK_SET);

		strcpy(sendBuff, s_STATUS_CODE_200);
		strcat(sendBuff, "\r\nDate: ");
		char* curDate = getDateTime();
		strcat(sendBuff, curDate);
		strcat(sendBuff, "\r\nContent-Type: text/html");
		strcat(sendBuff, "\r\nContent-Length: ");
		_itoa(fileSize, strFileSize, 10);
		strcat(sendBuff, strFileSize);
		strcat(sendBuff, "\r\n\r\n");

		while (fgets(tmpBuffer, MAX_SIZE, filePtr))
		{
			strcat(sendBuff, tmpBuffer);
		}

		fclose(filePtr);
	}
}

void doHEAD(int index, char* sendBuff)
{
	FILE* filePtr = NULL;
	char* fileName;
	char strFileSize[50];
	int fileSize = 0;

	fileName = sockets[index].buffer;
	fileName = strtok(fileName, " ");
	memmove(fileName, fileName + 1, strlen(fileName));

	if (fileName != NULL)
	{
		filePtr = fopen(fileName, "r");
	}

	if (filePtr == NULL)
	{
		strcpy(sendBuff, s_STATUS_CODE_404);
	}
	else
	{
		//get size of file
		fseek(filePtr, 0, SEEK_END);
		fileSize = ftell(filePtr);
		fseek(filePtr, 0, SEEK_SET);

		strcpy(sendBuff, s_STATUS_CODE_200);
		strcat(sendBuff, "\r\nDate: ");
		char* curDate = getDateTime();
		strcat(sendBuff, curDate);

		strcat(sendBuff, "Content-Length: ");
		_itoa(fileSize, strFileSize, 10);
		strcat(sendBuff, strFileSize);
		strcat(sendBuff, "\r\n\r\n");

		fclose(filePtr);
	}
}



void doPUT(int index, char* sendBuff)
{
	FILE* filePtr = NULL;
	char* fileName;
	char tmpBuffer[MAX_SIZE];
	char* messageBody;
	char strFileSize[50];
	bool fileExists = true;
	bool fileCreated = true;
	string bodyContent;
	int indexBody, fileSize;

	memcpy(tmpBuffer, sockets[index].buffer, strlen(sockets[index].buffer));

	fileName = tmpBuffer;
	fileName = strtok(fileName, " ");
	memmove(fileName, fileName + 1, strlen(fileName));

	filePtr = fopen(fileName, "r");
	if (filePtr == NULL)
	{
		fileExists = false;
	}
	else
	{
		fclose(filePtr);
	}

	filePtr = fopen(fileName, "w");
	if (filePtr == NULL)
	{
		fileCreated = false;
		strcpy(sendBuff, s_STATUS_CODE_500);
	}
	else
	{
		if (fileExists)
		{
			strcpy(sendBuff, s_STATUS_CODE_200);
		}
		else
		{
			strcpy(sendBuff, s_STATUS_CODE_201);
		}

		bodyContent = find_start_of_body(sockets[index].buffer);


		strcat(sendBuff, "\r\nDate: ");
		char* curDate = getDateTime();
		strcat(sendBuff, curDate);
		strcat(sendBuff, "\r\nContent-Type: text/html");
		strcat(sendBuff, "\r\nContent-Length: ");

		_itoa(bodyContent.length(), strFileSize, 10);

		strcat(sendBuff, strFileSize);

		strcat(sendBuff, "\r\n\r\n");

		fputs(bodyContent.c_str(), filePtr);
		
		
	}
	fclose(filePtr);
}

/**
 * Search for the start of the HTTP body.
 *
 * The body is after the header, separated from it by a blank line (two newlines in a row).
 *
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
string find_start_of_body(char *header) {
	char *start;

	if ((start = strstr(header, "\r\n\r\n")) != NULL) {
		return string(start + 2);
	}
	else if ((start = strstr(header, "\n\n")) != NULL) {
		return string(start + 2);
	}
	else if ((start = strstr(header, "\r\r")) != NULL) {
		return string(start + 2);
	}
	else {
		return string(start);
	}
}



char * getDateTime()
{
	size_t len = 0;
	size_t dateLen = 0;
	time_t t = time(NULL);

	struct tm *tm = localtime(&t);
	char s[64];

	//Sat, 09 Jun 2018 13:45:09 GMT
	strftime(s, sizeof(s), "%a, %d %b %Y %T", tm);
	char* ret = new char[strlen(s) + 1];
	strcpy(ret, s);
	ret[strlen(s)] = '\0';
	return ret;
}


void doTRACE(int index, char* sendBuff)
{
	ifstream requestedFile;
	char* response = new char[MAX_SIZE];
	char path[PATH_SIZE];
	char* contentLength = new char[3];

	getValidPath(index, path);
	requestedFile.open(path, ios::in);
	if (!requestedFile)
	{
		strcpy(response, s_STATUS_CODE_204);
	}
	else
	{
		sockets[index].buffer[sockets[index].len] = '\0';
		strcpy(response, s_STATUS_CODE_200);
		strcat(response, "\r\nDate: ");
		char* curDate = getDateTime();
		strcat(response, curDate);
		strcat(response, "\r\nContent-Type: text/html");
		strcat(response, "\r\nContent-Length: ");
		_itoa(5 + strlen(sockets[index].buffer), contentLength, 10);
		strcat(response, contentLength);

		strcat(response, "\r\n\r\n");
		strcat(response, "TRACE");
		strcat(response, sockets[index].buffer);
		requestedFile.close();
	}
	strcpy(sendBuff, response);
}



void doDELETE(int index, char* sendBuff)
{
	char* fileName;
	fileName = sockets[index].buffer;
	fileName = strtok(fileName, " ");
	memmove(fileName, fileName + 1, strlen(fileName));

	char* response = new char[MAX_SIZE];

	FILE* filePtr = NULL;
	bool fileExists = true;
	filePtr = fopen(fileName, "r");
	if (filePtr == NULL)
	{
		fileExists = false;
	}
	else
	{
		fclose(filePtr);
	}
	if (fileExists == false)
	{
		strcpy(response, s_STATUS_CODE_404);
	}
	else
	{
		remove(fileName);
		strcpy(response, s_STATUS_CODE_200);
		strcat(response, "\r\nDate: ");
		char* curDate = getDateTime();
		strcat(response, curDate);
		strcat(response, "\r\n\r\n");

		strcat(response, s_FILE_DELETED_SUCCESSFULLY);
	}

	strcpy(sendBuff, response);
}
