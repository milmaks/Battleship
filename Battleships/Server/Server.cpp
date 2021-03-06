#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include "Queue.h"
#include "../Common/Message.h"
#include "../Common/MessageFormater.cpp"
#include "../Server/BotFunctions.h"
#include "GameHelper.h"
#include <conio.h>
#include "../Common/Defines.h"
#include "../Common/Threads.cpp"
#include <assert.h>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27016"
#define MAX_CLIENTS 2

#pragma region functions

bool InitializeWindowsSockets();

/*
    Requests
    -------------------------------------------------------------------
    Requests - prima konekcije i odobrava ili odbija u yavisnosti od stanja u kojem se nalazi
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI Requests(LPVOID lpParam);

/*
    ProducerForClients
    -------------------------------------------------------------------
    ProducerForClients - cita poruku iz queue-a i vrsi proracune i odlucuje koju poruku da formira kom klijentu, i postavlja nove poruke u queue za klijente
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI ProducerForClients(LPVOID lpParam);

/*
    SendToPlayer1
    -------------------------------------------------------------------
    SendToPlayer1 - cita poruku iz queue-a za klijenta broj 1, i salje je putem tcp/ip protokola na klijentsku stranu.
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI SendToPlayer1(LPVOID lpParam);

/*
    SendToPlayer2
    -------------------------------------------------------------------
    SendToPlayer2 - cita poruku iz queue-a za klijenta broj 2, i salje je putem tcp/ip protokola na klijentsku stranu.
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI SendToPlayer2(LPVOID lpParam);

/*
    RecvFromPlayer1
    -------------------------------------------------------------------
    RecvFromPlayer1 - prima poruku od klijenta 1 preko tcp/ip protokola i postavlja je u queue za server da procita
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI RecvFromPlayer1(LPVOID lpParam);

/*
    RecvFromPlayer2
    -------------------------------------------------------------------
    RecvFromPlayer2 - prima poruku od klijenta 2 preko tcp/ip protokola i postavlja je u queue za server da procita
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI RecvFromPlayer2(LPVOID lpParam);

/*
    Bot
    -------------------------------------------------------------------
    Bot - omogucava se igra protiv servera.
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI Bot(LPVOID lpParam);

/*
    counterFunc
    -------------------------------------------------------------------
    counterFunc - prati preostalo vreme za potez za klijente, isto tako i odlucuje da li je kraj ako neki klijent preskoci dva puta potez
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI counterFunc(LPVOID lpParam);

/*
    ServiceShutDown
    -------------------------------------------------------------------
    ServiceShutDown - omogucava se server ugasi pravilno ako igra nije u toku i ako je pritisnuto 'q'
    -------------------------------------------------------------------
    Povratna vrednost: nema
*/
DWORD WINAPI ServiceShutDown(LPVOID lpParam);

#pragma endregion

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
HANDLE hCounterThread, hBot;
DWORD hCounterThreadID, hBotID;
HANDLE hServiceShutDown;
DWORD threadServiceShutDownID;

CRITICAL_SECTION csTimer;
int counter = 32;
ActionPlayer hisTurn = NONE;

bool closeHandlesPlayer1 = false;
bool closeHandlesPlayer2 = false;

bool gameInProgress = false;
bool gameInitializationInProgress = false;

int connCountPlayer1 = 0; //broj puta koji su se konektovali kljenti
int connCountPlayer2 = 0;

char boardPlayer1[10][10];
char boardPlayer2[10][10];

bool isServiceRunning = true;

int main(void) {

    DWORD threadRequestID, threadProducerID;
    HANDLE threadRequest, threadProducer;

    errorSemaphore = CreateSemaphore(NULL, 0, 1, NULL);

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
    hServiceShutDown = CreateThread(NULL, 0, &ServiceShutDown, NULL, 0, &threadServiceShutDownID);
    WaitForSingleObject(errorSemaphore, INFINITE);

    // shutdown the connection since we're done
    if(acceptedSocket[0] != INVALID_SOCKET)
        iResult = shutdown(acceptedSocket[0], SD_SEND);

    if (iResult == SOCKET_ERROR)
    {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(acceptedSocket[0]);
        WSACleanup();
        return 1;
    }

    if (acceptedSocket[1] != INVALID_SOCKET)
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

    SAFE_DELETE_HANDLE(threadRequest);
    SAFE_DELETE_HANDLE(threadProducer);
    SAFE_DELETE_HANDLE(hServiceShutDown);

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

    int socketSelected = 0;

    InitializeCriticalSection(&csTimer);

    while (isServiceRunning) {
        if (curentClientCount < MAX_CLIENTS + 1)
            FD_SET(listenSocket, &readfds);

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

                if (acceptedSocket[0] == INVALID_SOCKET) {
                    acceptedSocket[0] = accept(listenSocket, NULL, NULL);
                    connCountPlayer1++;
                    socketSelected = 0;
                }
                else if (acceptedSocket[1] == INVALID_SOCKET) {
                    acceptedSocket[1] = accept(listenSocket, NULL, NULL);
                    connCountPlayer2++;
                    socketSelected = 1;
                }

                if (acceptedSocket[socketSelected] == INVALID_SOCKET)
                {
                    printf("accept failed with error: %d\n", WSAGetLastError());
                    closesocket(listenSocket);
                    WSACleanup();
                    ReleaseSemaphore(errorSemaphore, 1, NULL);
                    return 1;
                }

                unsigned long mode = 1;
                iResult = ioctlsocket(acceptedSocket[socketSelected], FIONBIO, &mode);

                char msg[24];
                sprintf(msg, "true%d", socketSelected + 1);
                //welcome message
                iResult = send(acceptedSocket[socketSelected], msg, (int)strlen(msg) + 1, 0);

                if (iResult == SOCKET_ERROR)
                {
                    printf("send failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[curentClientCount]);
                    WSACleanup();
                    return 1;
                }

                if (curentClientCount == 0 && gameInProgress == false) {
                    hCounterThread = CreateThread(NULL, 0, &counterFunc, &counter, 0, &hCounterThreadID);
                    pauseThread(hCounterThread);
                }

                curentClientCount++;

                if (socketSelected == 0)
                {
                    closeHandlesPlayer1 = false;
                    threadSendPlayer1 = CreateThread(NULL, 0, &SendToPlayer1, NULL, 0, &threadSendPlayer1ID);
                    threadRecvPlayer1 = CreateThread(NULL, 0, &RecvFromPlayer1, NULL, 0, &threadRecvPlayer1ID);
                }
                if (socketSelected == 1)
                {
                    closeHandlesPlayer2 = false;
                    threadSendPlayer2 = CreateThread(NULL, 0, &SendToPlayer2, NULL, 0, &threadSendPlayer2ID);
                    threadRecvPlayer2 = CreateThread(NULL, 0, &RecvFromPlayer2, NULL, 0, &threadRecvPlayer2ID);
                }
            }
        }

        FD_ZERO(&readfds);

        Sleep(10);
    }

    DeleteCriticalSection(&csTimer);

    return 0;
}

DWORD WINAPI ProducerForClients(LPVOID lpParam) {
    hisTurn = FIRST;
    ActionPlayer inGameAgainsBot = NONE;
    int usedFileds = 0;
    while (isServiceRunning) {
        if (QueueCount(rootQueueRecv) != 0) {
            message* msg = Dequeue(&rootQueueRecv);
            int arg1;
            int arg2;

            //PLACE_BOAT init switch
            switch (msg->type)
            {
                case CHOOSE_GAME:
                {
                    if (gameInProgress == false)
                    {
                        message msgToSend;
                        if (strcmp(msg->argument, "1") == 0 && gameInitializationInProgress == false)
                        {
                            gameInProgress = true;
                            msgToSend = FormatMessageStruct(READY, msg->player, msg->player == FIRST ? 1 : 0, 0);

                        }
                        else if (strcmp(msg->argument, "2") == 0)
                        {
                            msgToSend = FormatMessageStruct(READY, msg->player, msg->player == FIRST ? 1 : 0, 0);
                            if (gameInitializationInProgress == false)
                                gameInitializationInProgress = true;
                            else
                                gameInProgress = true;  
                        }
                        else
                        {
                            msgToSend = FormatMessageStruct(BUSY, msg->player, 0, 0);
                        }

                        if (msg->player == FIRST)
                            Enqueue(&msgToSend, &rootQueuePlayer1);
                        else
                            Enqueue(&msgToSend, &rootQueuePlayer2);
                    }
                    else if (connCountPlayer1 > 1 && gameInProgress) {
                        if ((!gameInitializationInProgress && msg->player == inGameAgainsBot) || gameInitializationInProgress)
                        {
                            message msgConn = FormatMessageStruct(RECONNECT, msg->player, 0, 0);
                            if (msg->player == FIRST)
                                Enqueue(&msgConn, &rootQueuePlayer1);
                            else
                                Enqueue(&msgConn, &rootQueuePlayer2);

                            Sleep(20);

                            message msgMatrix = FormatMessageStruct(PLACE_BOAT_CLIENT, msg->player, msg->player == FIRST ? boardPlayer1 : boardPlayer2);
                            if (msg->player == FIRST)
                                Enqueue(&msgMatrix, &rootQueuePlayer1);
                            else
                                Enqueue(&msgMatrix, &rootQueuePlayer2);

                            Sleep(20);

                            message msgMatrixOponent = FormatMessageStruct(PLACE_BOAT_CLIENT_OPONENT, msg->player, msg->player == FIRST ? boardPlayer2 : boardPlayer1);
                            if (msg->player == FIRST)
                                Enqueue(&msgMatrixOponent, &rootQueuePlayer1);
                            else
                                Enqueue(&msgMatrixOponent, &rootQueuePlayer2);

                            Sleep(20);

                            if (msg->player == FIRST) {
                                if (hisTurn == FIRST) {
                                    message msgPlay = FormatMessageStruct(TURN_PLAY, FIRST, counter - 2, 0);
                                    Enqueue(&msgPlay, &rootQueuePlayer1);
                                }
                                else {
                                    message msgWait = FormatMessageStruct(TURN_WAIT, FIRST, 0, 0);
                                    Enqueue(&msgWait, &rootQueuePlayer1);
                                }
                            }
                            if (msg->player == SECOND) {
                                if (hisTurn == SECOND) {
                                    message msgPlay = FormatMessageStruct(TURN_PLAY, SECOND, counter - 2, 0);
                                    Enqueue(&msgPlay, &rootQueuePlayer2);
                                }
                                else {
                                    message msgWait = FormatMessageStruct(TURN_WAIT, SECOND, 0, 0);
                                    Enqueue(&msgWait, &rootQueuePlayer2);
                                }
                            }
                        }
                    }
                    else
                    {
                        message newMsg = FormatMessageStruct(BUSY, msg->player, 0, 0);
                        if (msg->player == FIRST) {
                            //Enqueue(&newMsg, &rootQueuePlayer1);
                            iResult = send(acceptedSocket[0], (char*)&newMsg, sizeof(newMsg), 0);
                            closeHandlesPlayer1 = true;
                            if (iResult == SOCKET_ERROR)
                            {
                                printf("send failed with error: %d\n", WSAGetLastError());
                                closesocket(acceptedSocket[0]);
                                WSACleanup();
                                //return 1;
                            }
                        }
                        else {
                            //Enqueue(&newMsg, &rootQueuePlayer2);
                            iResult = send(acceptedSocket[1], (char*)&newMsg, sizeof(newMsg), 0);
                            closeHandlesPlayer2 = true;
                            if (iResult == SOCKET_ERROR)
                            {
                                printf("send failed with error: %d\n", WSAGetLastError());
                                closesocket(acceptedSocket[0]);
                                WSACleanup();
                                //return 1;
                            }
                        }
                    }
                    break;
                }
                case PLACE_BOAT:
                {
                    hisTurn = FIRST;

                    for (int i = 0; i < 10; i++)
                    {
                        for (int j = 0; j < 10; j++)
                        {
                            if (msg->matrixArgumetn[i][j] == 3) {
                                //mesto broda
                                if (msg->player == FIRST)
                                    boardPlayer1[i][j] = 3;
                                if (msg->player == SECOND)
                                    boardPlayer2[i][j] = 3;

                                usedFileds++;
                            }
                            else {
                                if (msg->player == FIRST)
                                    boardPlayer1[i][j] = 0;
                                if (msg->player == SECOND)
                                    boardPlayer2[i][j] = 0;
                            }
                        }
                    }

                    if (!gameInitializationInProgress && gameInProgress)
                    {
                        //bot game
                        if (msg->player == FIRST) {
                            hBot = CreateThread(NULL, 0, &Bot, (LPVOID)2, 0, &hBotID);
                            //Sleep(1000);
                            message msg1 = FormatMessageStruct(PLAY, FIRST, 0, 0);
                            Enqueue(&msg1, &rootQueuePlayer1);
                            usedFileds = 0;
                            inGameAgainsBot = FIRST;
                            resumeThread(hCounterThread);
                        }
                        else {
                            hBot = CreateThread(NULL, 0, &Bot, (LPVOID)1, 0, &hBotID);
                            //Sleep(1000);
                            message msg2 = FormatMessageStruct(PLAY, SECOND, 0, 0);
                            Enqueue(&msg2, &rootQueuePlayer2);
                            usedFileds = 0;
                            inGameAgainsBot = SECOND;
                            resumeThread(hCounterThread);
                        }
                    }
                    if (gameInitializationInProgress)
                    {
                        if (usedFileds == 34) {
                            message msg1 = FormatMessageStruct(PLAY, FIRST, 0, 0);
                            message msg2 = FormatMessageStruct(PLAY, SECOND, 0, 0);
                            Enqueue(&msg1, &rootQueuePlayer1);
                            Enqueue(&msg2, &rootQueuePlayer2);
                            usedFileds = 0;
                            resumeThread(hCounterThread);
                        }
                    }
                    break;
                }

                case AIM_BOAT:
                {
                    EnterCriticalSection(&csTimer);
                    arg1 = msg->argument[0];
                    arg2 = msg->argument[1];
                    bool hit = false;

                    if (msg->player == FIRST) {
                        hit = boardPlayer2[arg1][arg2] == 3 ? true : false;

                        if (hit) {
                            boardPlayer2[arg1][arg2] = 2;
                            message msgHit = FormatMessageStruct(HIT, FIRST, arg1, arg2);
                            if (GameOver(boardPlayer2)) {
                                message msgV = FormatMessageStruct(VICTORY, FIRST, arg1, arg2);
                                message msgD = FormatMessageStruct(DEFEAT, SECOND, arg1, arg2);
                                Enqueue(&msgV, &rootQueuePlayer1);
                                Enqueue(&msgD, &rootQueuePlayer2);

                                connCountPlayer1 = 0;
                                connCountPlayer2 = 0;

                                Sleep(500);
                                if (!gameInitializationInProgress && gameInProgress)
                                {
                                    if (inGameAgainsBot == FIRST)
                                    {
                                        SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                        SAFE_DELETE_HANDLE(threadSendPlayer1);
                                    }
                                    else
                                    {
                                        SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                        SAFE_DELETE_HANDLE(threadSendPlayer2);
                                    }
                                    SAFE_DELETE_HANDLE(hCounterThread);
                                    SAFE_DELETE_HANDLE(hBot);
                                }
                                else
                                {
                                    SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                    SAFE_DELETE_HANDLE(threadSendPlayer1);
                                    SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                    SAFE_DELETE_HANDLE(threadSendPlayer2);
                                    SAFE_DELETE_HANDLE(hCounterThread);
                                }

                                gameInitializationInProgress = false;
                                gameInProgress = false;
                                counter = 32;
                            }
                            else {
                                hisTurn = FIRST;
                                Enqueue(&msgHit, &rootQueuePlayer1);
                                Enqueue(&msgHit, &rootQueuePlayer2);
                            }
                        }
                        else {
                            boardPlayer2[arg1][arg2] = 1;
                            hisTurn = SECOND;
                            message msgMiss = FormatMessageStruct(MISS, FIRST, arg1, arg2);
                            Enqueue(&msgMiss, &rootQueuePlayer1);
                            Enqueue(&msgMiss, &rootQueuePlayer2);
                        }
                    }

                    if (msg->player == SECOND) {
                        hit = boardPlayer1[arg1][arg2] == 3 ? true : false;

                        if (hit) {
                            boardPlayer1[arg1][arg2] = 2;
                            message msgHit = FormatMessageStruct(HIT, SECOND, arg1, arg2);
                            if (GameOver(boardPlayer1)) {
                                message msgV = FormatMessageStruct(VICTORY, SECOND, arg1, arg2);
                                message msgD = FormatMessageStruct(DEFEAT, FIRST, arg1, arg2);
                                Enqueue(&msgD, &rootQueuePlayer1);
                                Enqueue(&msgV, &rootQueuePlayer2);

                                connCountPlayer1 = 0;
                                connCountPlayer2 = 0;

                                Sleep(500);
                                if (!gameInitializationInProgress && gameInProgress)
                                {
                                    if (inGameAgainsBot == FIRST)
                                    {
                                        SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                        SAFE_DELETE_HANDLE(threadSendPlayer1);
                                    }
                                    else
                                    {
                                        SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                        SAFE_DELETE_HANDLE(threadSendPlayer2);
                                    }
                                    SAFE_DELETE_HANDLE(hCounterThread);
                                    SAFE_DELETE_HANDLE(hBot);
                                }
                                else
                                {
                                    SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                    SAFE_DELETE_HANDLE(threadSendPlayer1);
                                    SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                    SAFE_DELETE_HANDLE(threadSendPlayer2);
                                    SAFE_DELETE_HANDLE(hCounterThread);
                                }

                                gameInitializationInProgress = false;
                                gameInProgress = false;
                                counter = 32;
                            }
                            else {
                                hisTurn = SECOND;
                                Enqueue(&msgHit, &rootQueuePlayer1);
                                Enqueue(&msgHit, &rootQueuePlayer2);
                            }
                        }
                        else {
                            boardPlayer1[arg1][arg2] = 1;
                            hisTurn = FIRST;
                            message msgMiss = FormatMessageStruct(MISS, SECOND, arg1, arg2);
                            Enqueue(&msgMiss, &rootQueuePlayer1);
                            Enqueue(&msgMiss, &rootQueuePlayer2);
                        }
                    }

                    counter = 32;
                    LeaveCriticalSection(&csTimer);

                    break;
                }               
                case READY:
                {
                    if (msg->player == FIRST)
                        Enqueue(msg, &rootQueuePlayer1);
                    if (msg->player == SECOND)
                        Enqueue(msg, &rootQueuePlayer2);
                    break;
                }
                case BUSY:
                {
                    if (msg->player == FIRST)
                        Enqueue(msg, &rootQueuePlayer1);
                    if (msg->player == SECOND)
                        Enqueue(msg, &rootQueuePlayer2);
                    break;
                }
                case PLACE_BOAT_CLIENT:
                {
                    if (msg->player == FIRST)
                        Enqueue(msg, &rootQueuePlayer1);
                    if (msg->player == SECOND)
                        Enqueue(msg, &rootQueuePlayer2);
                    break;
                }
                case PLACE_BOAT_CLIENT_OPONENT:
                {
                    if (msg->player == NONE)
                    {
                        if (hisTurn == FIRST) {
                            hisTurn = SECOND;
                            message msgPlay = FormatMessageStruct(TURN_PLAY, SECOND, 30, 0);
                            message msgWait = FormatMessageStruct(TURN_WAIT, FIRST, 0, 0);
                            Enqueue(&msgWait, &rootQueuePlayer1);
                            Enqueue(&msgPlay, &rootQueuePlayer2);
                        }
                        else {
                            hisTurn = FIRST;
                            message msgPlay = FormatMessageStruct(TURN_PLAY, FIRST, 30, 0);
                            message msgWait = FormatMessageStruct(TURN_WAIT, SECOND, 0, 0);
                            Enqueue(&msgWait, &rootQueuePlayer2);
                            Enqueue(&msgPlay, &rootQueuePlayer1);
                        }
                    }
                    if (msg->player == FIRST) {
                        Enqueue(msg, &rootQueuePlayer1);
                        if (hisTurn == FIRST) {
                            message msgPlay = FormatMessageStruct(TURN_PLAY, FIRST, counter - 2, 0);
                            Enqueue(&msgPlay, &rootQueuePlayer1);
                        }
                        else {
                            message msgWait = FormatMessageStruct(TURN_WAIT, FIRST, 0, 0);
                            Enqueue(&msgWait, &rootQueuePlayer1);
                        }
                    }
                    if (msg->player == SECOND) {
                        Enqueue(msg, &rootQueuePlayer2);
                        if (hisTurn == SECOND) {
                            message msgPlay = FormatMessageStruct(TURN_PLAY, SECOND, counter - 2, 0);
                            Enqueue(&msgPlay, &rootQueuePlayer2);
                        }
                        else {
                            message msgWait = FormatMessageStruct(TURN_WAIT, SECOND, 0, 0);
                            Enqueue(&msgWait, &rootQueuePlayer2);
                        }
                    }
                    break;
                }
                case RECONNECT:
                {
                    if (msg->player == FIRST)
                        Enqueue(msg, &rootQueuePlayer1);
                    if (msg->player == SECOND)
                        Enqueue(msg, &rootQueuePlayer2);
                    break;
                }
                case DEFEAT:
                {
                    if (msg->player == FIRST)
                    {
                        message msgV = FormatMessageStruct(VICTORY, SECOND, 0, 0);
                        message msgD = FormatMessageStruct(DEFEAT, FIRST, 0, 0);
                        Enqueue(&msgD, &rootQueuePlayer1);
                        Enqueue(&msgV, &rootQueuePlayer2);

                        
                        connCountPlayer1 = 0;
                        connCountPlayer2 = 0;

                        Sleep(500);
                        if (!gameInitializationInProgress && gameInProgress)
                        {
                            if (inGameAgainsBot == FIRST)
                            {
                                SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                SAFE_DELETE_HANDLE(threadSendPlayer1);
                            }
                            else
                            {
                                SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                SAFE_DELETE_HANDLE(threadSendPlayer2);
                            }
                            SAFE_DELETE_HANDLE(hCounterThread);
                            SAFE_DELETE_HANDLE(hBot);
                        }
                        else
                        {
                            SAFE_DELETE_HANDLE(threadRecvPlayer1);
                            SAFE_DELETE_HANDLE(threadSendPlayer1);
                            SAFE_DELETE_HANDLE(threadRecvPlayer2);
                            SAFE_DELETE_HANDLE(threadSendPlayer2);
                            SAFE_DELETE_HANDLE(hCounterThread);
                        }

                        gameInitializationInProgress = false;
                        gameInProgress = false;
                        counter = 32;
                    }
                    else
                    {
                        message msgV = FormatMessageStruct(VICTORY, FIRST, 0, 0);
                        message msgD = FormatMessageStruct(DEFEAT, SECOND, 0, 0);
                        Enqueue(&msgV, &rootQueuePlayer1);
                        Enqueue(&msgD, &rootQueuePlayer2);

                        connCountPlayer1 = 0;
                        connCountPlayer2 = 0;

                        Sleep(500);
                        if (!gameInitializationInProgress && gameInProgress)
                        {
                            if (inGameAgainsBot == FIRST)
                            {
                                SAFE_DELETE_HANDLE(threadRecvPlayer1);
                                SAFE_DELETE_HANDLE(threadSendPlayer1);
                            }
                            else
                            {
                                SAFE_DELETE_HANDLE(threadRecvPlayer2);
                                SAFE_DELETE_HANDLE(threadSendPlayer2);
                            }
                            SAFE_DELETE_HANDLE(hCounterThread);
                            SAFE_DELETE_HANDLE(hBot);
                        }
                        else
                        {
                            SAFE_DELETE_HANDLE(threadRecvPlayer1);
                            SAFE_DELETE_HANDLE(threadSendPlayer1);
                            SAFE_DELETE_HANDLE(threadRecvPlayer2);
                            SAFE_DELETE_HANDLE(threadSendPlayer2);
                            SAFE_DELETE_HANDLE(hCounterThread);
                        }

                        gameInitializationInProgress = false;
                        gameInProgress = false;
                        counter = 32;
                    }
                    break;
                }
            }
        }
                

        Sleep(10);
    }

    return 0;
}

#pragma region SendToPlayer

DWORD WINAPI SendToPlayer1(LPVOID lpParam) {
    while (!closeHandlesPlayer1 && acceptedSocket[0] != INVALID_SOCKET) {
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
        Sleep(40);
    }

    return 0;
}

DWORD WINAPI SendToPlayer2(LPVOID lpParam) {
    while (!closeHandlesPlayer2 && acceptedSocket[1] != INVALID_SOCKET) {
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
        Sleep(40);
    }

    return 0;
}

#pragma endregion

#pragma region RecieveFromPlayer

DWORD WINAPI RecvFromPlayer1(LPVOID lpParam) {
    message* recvmsg;
    message* msgToSend;
    fd_set readfds;
    FD_ZERO(&readfds);

    timeval timeVal;
    timeVal.tv_sec = 1;
    timeVal.tv_usec = 0;
    while (!closeHandlesPlayer1 && acceptedSocket[0] != INVALID_SOCKET) {
        FD_SET(acceptedSocket[0], &readfds);

        int result = select(0, &readfds, NULL, NULL, &timeVal);

        if (result == 0) {
            // vreme za cekanje je isteklo
            //Sleep(10);
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
                   
                    Enqueue(recvmsg, &rootQueueRecv);
                    printf("Message received from client and queued in queue!\n");
                }
                else if (iResult == 0)
                {
                    // connection was closed gracefully
                    printf("Connection with client closed.\n");
                    closesocket(acceptedSocket[0]);
                    acceptedSocket[0] = INVALID_SOCKET;
                    closeHandlesPlayer1 = true;
                    curentClientCount--;
                }
                else
                {
                    // there was an error during recv
                    printf("recv failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[0]);
                    acceptedSocket[0] = INVALID_SOCKET;
                    closeHandlesPlayer1 = true;
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
    while (!closeHandlesPlayer2 && acceptedSocket[1] != INVALID_SOCKET) {
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
                    
                    //stavljanje u queue
                    Enqueue(recvmsg, &rootQueueRecv);
                    printf("Message received from client and queued in queue\n");
                }
                else if (iResult == 0)
                {
                    // connection was closed gracefully
                    printf("Connection with client closed.\n");
                    closesocket(acceptedSocket[1]);
                    acceptedSocket[1] = INVALID_SOCKET;
                    closeHandlesPlayer2 = true;
                    curentClientCount--;
                }
                else
                {
                    // there was an error during recv
                    printf("recv failed with error: %d\n", WSAGetLastError());
                    closesocket(acceptedSocket[1]);
                    acceptedSocket[1] = INVALID_SOCKET;
                    closeHandlesPlayer2 = true;
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

#pragma endregion

DWORD WINAPI counterFunc(LPVOID lpParam) {
    int* counter = (int*)lpParam;
    int pom = 0, moveRedirected1 = 0, moveRedirected2 = 0;

    message errMsg, msgEnd;
    while (gameInProgress || gameInitializationInProgress) {
        while (pom != 100) {
            Sleep(10);
            pom++;
        }

        EnterCriticalSection(&csTimer);
        if (*counter == 0) {
            if (moveRedirected1 == 1 || moveRedirected2 == 1) {
                if (moveRedirected1 == 1)
                    msgEnd = FormatMessageStruct(DEFEAT, FIRST, 0, 0);
                else
                    msgEnd = FormatMessageStruct(DEFEAT, SECOND, 0, 0);
                Enqueue(&msgEnd, &rootQueueRecv);
                moveRedirected1 = 0;
                moveRedirected2 = 0;
                LeaveCriticalSection(&csTimer);
                Sleep(50);
                break;
            }
            if (hisTurn == FIRST)
                moveRedirected1++;
            else
                moveRedirected2++;
            *counter = 32;
            //redirect move to other player
            errMsg = FormatMessageStruct(PLACE_BOAT_CLIENT_OPONENT, NONE, 0,0);

            Enqueue(&errMsg, &rootQueueRecv);
            LeaveCriticalSection(&csTimer);
            //Sleep(300);
            continue;
        }
        *counter = (*counter)--;
        printf("    counter - %d\n", *counter);
        LeaveCriticalSection(&csTimer);
        pom = 0;
    }

    return 0;
}

DWORD WINAPI Bot(LPVOID lpParam) {
    ActionPlayer player = NONE;
    if ((int)lpParam == 1)
        player = FIRST;
    else
        player = SECOND;

    bool play = true;
    char aimingTable[10][10];
    MATRIX_FIELD fields;
    message msg;

    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            aimingTable[i][j] = 0;
        }
    }

    if (player == FIRST)
    {
        botTableInitialization(boardPlayer1[0]);
        while (play)
        {
            //igra
            fields = botAim(aimingTable[0]);
            msg = FormatMessageStruct(AIM_BOAT, player, fields.I, fields.J);
            Sleep(1000);
            Enqueue(&msg, &rootQueueRecv);

            //ceka
            while (true) {
                if (QueueCount(rootQueuePlayer1) != 0) {
                    msg = *Dequeue(&rootQueuePlayer1);
                    if (msg.player == SECOND && msg.type == HIT)
                        continue;
                    if (msg.player == SECOND && msg.type == MISS)
                        break;
                    if (msg.player == FIRST && msg.type == HIT)
                        break;
                    if (msg.player == FIRST && msg.type == MISS)
                        continue;
                    if (msg.type == VICTORY || msg.type == DEFEAT) {
                        play = false;
                        break;
                    }
                    if (msg.type == TURN_PLAY)
                        break;
                    if (msg.type == CHOOSE_GAME || msg.type == BUSY) {
                        Enqueue(&msg, &rootQueueRecv);
                        continue;
                    }
                }
                Sleep(40);
            }

        }
    }
    else //(player == SECOND)
    {
        botTableInitialization(boardPlayer2[0]);
        while (play)
        {
            //ceka
            while (true) {
                if (QueueCount(rootQueuePlayer2) != 0) {
                    msg = *Dequeue(&rootQueuePlayer2);

                    if (msg.player == SECOND && msg.type == HIT) 
                        break;
                    if (msg.player == SECOND && msg.type == MISS)
                        continue;
                    if (msg.player == FIRST && msg.type == HIT)
                        continue;
                    if (msg.player == FIRST && msg.type == MISS)
                        break;
                    if (msg.type == VICTORY || msg.type == DEFEAT) {
                        play = false;
                        break;
                    }
                    if (msg.type == TURN_PLAY)
                        break;
                    if (msg.type == CHOOSE_GAME || msg.type == BUSY) {
                        Enqueue(&msg, &rootQueueRecv);
                        continue;
                    }
                }
                Sleep(40);
            }

            if (!play)
                continue;

            //igra
            fields = botAim(aimingTable[0]);
            msg = FormatMessageStruct(AIM_BOAT, player, fields.I, fields.J);
            Sleep(1000);
            Enqueue(&msg, &rootQueueRecv);
        }
    }

    return 0;
}

DWORD WINAPI ServiceShutDown(LPVOID lpParam) {
    while (true) {
        if (_kbhit()) {
            char key = _getch();
            if (key == 'q' && !gameInProgress)
                break;
        }
        Sleep(250);
    }

    isServiceRunning = false;
    printf("Service is shuting down...\n");

    Sleep(500);

    ReleaseSemaphore(errorSemaphore, 1, NULL);  

    return 0;
}