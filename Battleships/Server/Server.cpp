#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include "Queue.h"
#include "../Common/Message.h"

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27016"
#define MAX_CLIENTS 2

bool InitializeWindowsSockets();
DWORD WINAPI Requests(LPVOID lpParam);
DWORD WINAPI ProducerForClients(LPVOID lpParam);
DWORD WINAPI SendToPlayer1(LPVOID lpParam);
DWORD WINAPI SendToPlayer2(LPVOID lpParam);
DWORD WINAPI RecvFromPlayer1(LPVOID lpParam);
DWORD WINAPI RecvFromPlayer2(LPVOID lpParam);
 
SOCKET listenSocket = INVALID_SOCKET;

SOCKET acceptedSocket[MAX_CLIENTS];
SOCKET acceptedSocketRefuse;

int curentClientCount = 0;
int iResult;

element* rootQueueRecv;
element* rootQueuePlayer1;
element* rootQueuePlayer2;
char recvbuf[DEFAULT_BUFLEN];

HANDLE errorSemaphore, threadSendPlayer1, threadSendPlayer2;
DWORD threadSendPlayer1ID, threadSendPlayer2ID;
HANDLE threadRecvPlayer1, threadRecvPlayer2;
DWORD threadRecvPlayer1ID, threadRecvPlayer2ID;

bool gameInProgress = false;
bool gameInitializationInProgress = false;

int main(void) {
    DWORD threadRequestID, threadProducerID;
    HANDLE threadRequest, threadProducer;

    errorSemaphore = CreateSemaphore(NULL, 0, 1, NULL);

    message msg;

    InitQueue(&rootQueueRecv);
    InitQueue(&rootQueuePlayer1);
    InitQueue(&rootQueuePlayer2);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        acceptedSocket[i] = INVALID_SOCKET;
    }

    if (InitializeWindowsSockets() == false)
    {
        // we won't log anything since it will be logged
        // by InitializeWindowsSockets() function
        return 1;
    }

    // Prepare address information structures
    addrinfo* resultingAddress = NULL;
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4 address
    hints.ai_socktype = SOCK_STREAM; // Provide reliable data streaming
    hints.ai_protocol = IPPROTO_TCP; // Use TCP protocol
    hints.ai_flags = AI_PASSIVE;     // 

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &resultingAddress);
    if (iResult != 0)
    {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for connecting to server
    listenSocket = socket(AF_INET,      // IPv4 address famly
        SOCK_STREAM,  // stream socket
        IPPROTO_TCP); // TCP

    if (listenSocket == INVALID_SOCKET)
    {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(resultingAddress);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket - bind port number and local address 
    // to socket
    iResult = bind(listenSocket, resultingAddress->ai_addr, (int)resultingAddress->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(resultingAddress);
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Since we don't need resultingAddress any more, free it
    freeaddrinfo(resultingAddress);

    // Set listenSocket in listening mode
    iResult = listen(listenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR)
    {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    unsigned long mode = 1;
    iResult = ioctlsocket(listenSocket, FIONBIO, &mode);

    printf("Server initialized, waiting for clients.\n");

    //thread for requests made in select function
    threadRequest = CreateThread(NULL, 0, &Requests, NULL, 0, &threadRequestID);
    threadProducer = CreateThread(NULL, 0, &ProducerForClients, NULL, 0, &threadProducerID);
    WaitForSingleObject(errorSemaphore, INFINITE);

    // shutdown the connection since we're done
    iResult = shutdown(acceptedSocket[0], SD_SEND);

    if (iResult == SOCKET_ERROR)
    {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(acceptedSocket[0]);
        WSACleanup();
        return 1;
    }

    iResult = shutdown(acceptedSocket[1], SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(acceptedSocket[1]);
        WSACleanup();
        return 1;
    }

    // cleanup
    closesocket(listenSocket);
    closesocket(acceptedSocket[0]);
    closesocket(acceptedSocket[1]);
    WSACleanup();

    return 0;
}

bool InitializeWindowsSockets() {
    WSADATA wsaData;
    // Initialize windows sockets library for this process
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup failed with error: %d\n", WSAGetLastError());
        return false;
    }
    return true;
}

DWORD WINAPI Requests(LPVOID lpParam) {
    fd_set readfds;
    FD_ZERO(&readfds);

    timeval timeVal;
    timeVal.tv_sec = 1;
    timeVal.tv_usec = 0;
    while (true) {
        if (curentClientCount < MAX_CLIENTS + 1)
            FD_SET(listenSocket, &readfds);

        for (int i = 0; i < curentClientCount; i++)
        {
            FD_SET(acceptedSocket[i], &readfds);
        }

        int result = select(0, &readfds, NULL, NULL, &timeVal);

        if (result == 0) {
            // vreme za cekanje je isteklo
            continue;
        }
        else if (result == SOCKET_ERROR) {
            //desila se greska prilikom poziva funkcije
            ReleaseSemaphore(errorSemaphore, 1, NULL);
            break;
        }
        else {
            // rezultat je jednak broju soketa koji su zadovoljili uslov
            if (FD_ISSET(listenSocket, &readfds)) {
                if (curentClientCount == MAX_CLIENTS) {

                    //there is 3. client wwnt to connect, accept connection and then close it
                    acceptedSocketRefuse = accept(listenSocket, NULL, NULL);

                    if (acceptedSocketRefuse == INVALID_SOCKET)
                    {
                        printf("accept failed with error: %d\n", WSAGetLastError());
                        closesocket(listenSocket);
                        WSACleanup();
                        ReleaseSemaphore(errorSemaphore, 1, NULL);
                        return 1;
                    }

                    iResult = send(acceptedSocketRefuse, "false", (int)strlen("false") + 1, 0);

                    if (iResult == SOCKET_ERROR)
                    {
                        printf("send failed with error: %d\n", WSAGetLastError());
                        closesocket(acceptedSocketRefuse);
                        WSACleanup();
                        return 1;
                    }

                    closesocket(acceptedSocketRefuse);

                    printf("3. client is refused.");

                    continue;
                }

                acceptedSocket[curentClientCount] = accept(listenSocket, NULL, NULL);

                if (acceptedSocket[curentClientCount] == INVALID_SOCKET)
                {
                    printf("accept failed with error: %d\n", WSAGetLastError());
                    closesocket(listenSocket);
                    WSACleanup();
                    ReleaseSemaphore(errorSemaphore, 1, NULL);
                    return 1;
                }

                unsigned long mode = 1;
                iResult = ioctlsocket(acceptedSocket[curentClientCount], FIONBIO, &mode);

                //welcome message
                iResult = send(acceptedSocket[curentClientCount], "true", (int)strlen("true") + 1, 0);

                if (iResult == SOCKET_ERROR)
                {
                    printf("send failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[curentClientCount]);
                    WSACleanup();
                    return 1;
                }

                curentClientCount++;

                if (curentClientCount == 1)
                {
                    threadSendPlayer1 = CreateThread(NULL, 0, &SendToPlayer1, NULL, 0, &threadSendPlayer1ID);
                    threadRecvPlayer1 = CreateThread(NULL, 0, &RecvFromPlayer1, NULL, 0, &threadRecvPlayer1ID);
                }
                if (curentClientCount == 2)
                {
                    threadSendPlayer2 = CreateThread(NULL, 0, &SendToPlayer2, NULL, 0, &threadSendPlayer2ID);
                    threadRecvPlayer2 = CreateThread(NULL, 0, &RecvFromPlayer2, NULL, 0, &threadRecvPlayer2ID);
                }
            }

            /*
            for (int i = 0; i < curentClientCount; i++) {
                if (FD_ISSET(acceptedSocket[i], &readfds)) {
                    iResult = recv(acceptedSocket[i], recvbuf, DEFAULT_BUFLEN, 0);
                    if (iResult > 0)
                    {
                        //stavljanje u queue
                        Enqueue(recvbuf, &rootQueueRecv);
                        printf("Message received from client and qnqueued in queue\n");
                    }
                    else if (iResult == 0)
                    {
                        // connection was closed gracefully
                        printf("Connection with client closed.\n");
                        closesocket(acceptedSocket[i]);

                        if (i == 0 && acceptedSocket[1] != INVALID_SOCKET) {
                            acceptedSocket[0] = acceptedSocket[1];
                            acceptedSocket[1] = INVALID_SOCKET;
                        }
                        curentClientCount--;
                    }
                    else
                    {
                        // there was an error during recv
                        printf("recv failed with error: %d\n", WSAGetLastError());
                        closesocket(acceptedSocket[i]);
                    }
                }
            }
            */
        }

        FD_ZERO(&readfds);

        Sleep(10);
    }

    return 0;
}

DWORD WINAPI ProducerForClients(LPVOID lpParam) {
    while (true) {
        if (QueueCount(rootQueueRecv) != 0) {
            message* msg = Dequeue(&rootQueueRecv);

            if(msg->player == FIRST)
                Enqueue(msg, &rootQueuePlayer1);
            if(msg->player == SECOND)
                Enqueue(msg, &rootQueuePlayer2);
        }

        Sleep(10);
    }
}

DWORD WINAPI SendToPlayer1(LPVOID lpParam) {
    while (true) {
        if (QueueCount(rootQueuePlayer1) != 0) {
            message sendP1 = *Dequeue(&rootQueuePlayer1);

            iResult = send(acceptedSocket[0], (char*)&sendP1, sizeof(sendP1), 0);

            if (iResult == SOCKET_ERROR)
            {
                printf("send failed with error: %d\n", WSAGetLastError());
                closesocket(acceptedSocket[0]);
                WSACleanup();
                return 1;
            }
        }
        Sleep(10);
    }

    return 0;
}

DWORD WINAPI SendToPlayer2(LPVOID lpParam) {
    while (true) {
        if (QueueCount(rootQueuePlayer2) != 0) {
            message sendP2 = *Dequeue(&rootQueuePlayer2);

            iResult = send(acceptedSocket[1], (char*)&sendP2, sizeof(sendP2), 0);

            if (iResult == SOCKET_ERROR)
            {
                printf("send failed with error: %d\n", WSAGetLastError());
                closesocket(acceptedSocket[1]);
                WSACleanup();
                return 1;
            }
        }
    }

    return 0;
}

DWORD WINAPI RecvFromPlayer1(LPVOID lpParam) {
    message* recvmsg;
    message* msgToSend;
    fd_set readfds;
    FD_ZERO(&readfds);

    timeval timeVal;
    timeVal.tv_sec = 1;
    timeVal.tv_usec = 0;
    while (true) {
        FD_SET(acceptedSocket[0], &readfds);

        int result = select(0, &readfds, NULL, NULL, &timeVal);

        if (result == 0) {
            // vreme za cekanje je isteklo
            Sleep(10);
            continue;
        }
        else if (result == SOCKET_ERROR) {
            //desila se greska prilikom poziva funkcije
            ReleaseSemaphore(errorSemaphore, 1, NULL);
            break;
        }
        else
        {
            if (FD_ISSET(acceptedSocket[0], &readfds)) {
                iResult = recv(acceptedSocket[0], recvbuf, DEFAULT_BUFLEN, 0);
                if (iResult > 0)
                {
                    recvbuf[iResult] = '\0';
                    recvmsg = (message*)recvbuf;

                    if (recvmsg->type == CHOOSE_GAME)
                    {
                        if (gameInProgress == false)
                        {
                            if (strcmp(recvmsg->argument, "1") == 0 && gameInitializationInProgress == false)
                            //if (ntohl(recvmsg->argumet) == 1 && gameInitializationInProgress == false)
                            {
                                gameInProgress = true;
                                msgToSend = (message*)malloc(sizeof(message));
                                msgToSend->type = READY;
                                msgToSend->player = FIRST;

                                Enqueue(msgToSend, &rootQueueRecv);

                            }
                            else if (strcmp(recvmsg->argument, "2") == 0)
                            //if (ntohl(recvmsg->argumet) == 2)
                            {
                                msgToSend = (message*)malloc(sizeof(message));
                                if (gameInitializationInProgress == false)
                                    gameInitializationInProgress = true;
                                else
                                    gameInProgress = true;
                                msgToSend->player = FIRST;
                                msgToSend->type = READY;
                                Enqueue(msgToSend, &rootQueueRecv);
                            }
                            else {
                                msgToSend = (message*)malloc(sizeof(message));
                                msgToSend->type = BUSY;
                                msgToSend->player = FIRST;
                                Enqueue(msgToSend, &rootQueueRecv);
                            }
                        }
                        else
                        {
                            msgToSend = (message*)malloc(sizeof(message));
                            msgToSend->type = BUSY;
                            msgToSend->player = FIRST;
                            Enqueue(msgToSend, &rootQueueRecv);
                        }
                        printf("Message received from client and queued in queue\n");
                    }
                    else
                    {
                        //stavljanje u queue
                        Enqueue(recvmsg, &rootQueueRecv);
                        printf("Message received from client and queued in queue\n");
                    }
                }
                else if (iResult == 0)
                {
                    // connection was closed gracefully
                    printf("Connection with client closed.\n");
                    closesocket(acceptedSocket[0]);

                    if (acceptedSocket[1] != INVALID_SOCKET) {
                        acceptedSocket[0] = acceptedSocket[1];
                        acceptedSocket[1] = INVALID_SOCKET;
                    }
                    curentClientCount--;
                }
                else
                {
                    // there was an error during recv
                    printf("recv failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[0]);
                    if (acceptedSocket[1] != INVALID_SOCKET) {
                        acceptedSocket[0] = acceptedSocket[1];
                        acceptedSocket[1] = INVALID_SOCKET;
                    }
                    curentClientCount--;
                    break;
                }
            }
            Sleep(10);

        }
        FD_ZERO(&readfds);
    }

    return 0;
}

DWORD WINAPI RecvFromPlayer2(LPVOID lpParam) {
    message* recvmsg;
    message* msgToSend;
    fd_set readfds;
    FD_ZERO(&readfds);

    timeval timeVal;
    timeVal.tv_sec = 1;
    timeVal.tv_usec = 0;
    while (true) {
        FD_SET(acceptedSocket[1], &readfds);

        int result = select(0, &readfds, NULL, NULL, &timeVal);

        if (result == 0) {
            // vreme za cekanje je isteklo
            Sleep(10);
            continue;
        }
        else if (result == SOCKET_ERROR) {
            //desila se greska prilikom poziva funkcije
            ReleaseSemaphore(errorSemaphore, 1, NULL);
            break;
        }
        else
        {
            if (FD_ISSET(acceptedSocket[1], &readfds)) {
                iResult = recv(acceptedSocket[1], recvbuf, DEFAULT_BUFLEN, 0);
                if (iResult > 0)
                {
                    recvbuf[iResult] = '\0';
                    recvmsg = (message*)recvbuf;

                    if (recvmsg->type == CHOOSE_GAME)
                    {
                        if (gameInProgress == false)
                        {
                            if (strcmp(recvmsg->argument, "1") == 0 && gameInitializationInProgress == false)
                            //if (ntohl(recvmsg->argument) == 1 && gameInitializationInProgress == false)
                            {
                                gameInProgress = true;
                                msgToSend = (message*)malloc(sizeof(message));
                                msgToSend->type = READY;
                                msgToSend->player = SECOND;

                                Enqueue(msgToSend, &rootQueueRecv);

                            }
                            else if (strcmp(recvmsg->argument, "2") == 0)
                            //if (ntohl(recvmsg->argument) == 2)
                            {
                                msgToSend = (message*)malloc(sizeof(message));
                                if (gameInitializationInProgress == false)
                                    gameInitializationInProgress = true;
                                else
                                    gameInProgress = true;
                                msgToSend->player = SECOND;
                                msgToSend->type = READY;
                                Enqueue(msgToSend, &rootQueueRecv);
                            }
                            else {
                                msgToSend = (message*)malloc(sizeof(message));
                                msgToSend->type = BUSY;
                                msgToSend->player = SECOND;
                                Enqueue(msgToSend, &rootQueueRecv);
                            }
                        }
                        else
                        {
                            msgToSend = (message*)malloc(sizeof(message));
                            msgToSend->type = BUSY;
                            msgToSend->player = SECOND;
                            Enqueue(msgToSend, &rootQueueRecv);
                        }
                        printf("Message received from client and queued in queue\n");
                    }
                    else
                    {
                        //stavljanje u queue
                        Enqueue(recvmsg, &rootQueueRecv);
                        printf("Message received from client and queued in queue\n");
                    }
                }
                else if (iResult == 0)
                {
                    // connection was closed gracefully
                    printf("Connection with client closed.\n");
                    closesocket(acceptedSocket[1]);
                    curentClientCount--;
                }
                else
                {
                    // there was an error during recv
                    printf("recv failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[1]);
                    curentClientCount--;
                    break;
                }
            }
            Sleep(10);

        }
        FD_ZERO(&readfds);
    }

    return 0;
}