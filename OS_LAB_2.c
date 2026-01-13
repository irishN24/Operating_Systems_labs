#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

// максимальное количество одновременных подключений равно 2
const int MAX_CONCURRENT_CLIENTS = 2;

// флаг для обработки сигналов
volatile sig_atomic_t sigHupReceived = 0;

// структура для информации о подключенном клиенте
typedef struct
{
    int connectionSocket;          // дескриптор сокета клиента
    struct sockaddr_in clientAddress; // информация о клиенте
} ClientConnection;

// обработчик сигнала 
void handleHangupSignal(int signalNumber)
{
    sigHupReceived = 1;
}

// настройка обработчика сигналов с блокировкой
void setupSignalHandling(sigset_t* originalSignalMask)
{
    struct sigaction signalAction;
    sigaction(SIGHUP, NULL, &signalAction);
    signalAction.sa_handler = handleHangupSignal;
    signalAction.sa_flags |= SA_RESTART;
    sigaction(SIGHUP, &signalAction, NULL);

    // временная блокировка сигнала для безопасной обработки
    sigset_t blockedSignals;
    sigemptyset(&blockedSignals);
    sigaddset(&blockedSignals, SIGHUP);
    sigprocmask(SIG_BLOCK, &blockedSignals, originalSignalMask);
}

// создание и настройка TCP серверного сокета
int createListeningSocket(int serverPort)
{
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    int listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket < 0)
    {
        perror("Couldn't create socket!!!");
        exit(EXIT_FAILURE);
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(serverPort);
    if (bind(listeningSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) != 0)
    {
        perror("Error binding the address to the socket!!!");
        close(listeningSocket);
        exit(EXIT_FAILURE);
    }
    if (listen(listeningSocket, MAX_CONCURRENT_CLIENTS) != 0)
    {
        perror("Couldn't switch the socket to listening mode!!!");
        close(listeningSocket);
        exit(EXIT_FAILURE);
    }
    return listeningSocket;
}

// основной цикл обработки клиентских подключений
int processClientConnections(int listeningSocketDescriptor, sigset_t originalSignalMask)
{
    ClientConnection activeConnections[MAX_CONCURRENT_CLIENTS];
    int connectedClientCount = 0;
    char messageDataBuffer[1024] = { 0 };
    while (1)
    {
        // обработка запроса на вывод информации о клиентах
        if (sigHupReceived)
        {
            printf("Активные клиентские подключения: ");
            for (int connectionIndex = 0; connectionIndex < connectedClientCount; connectionIndex++)
            {
                printf("Клиент #%d ", connectionIndex + 1);
            }
            printf("\nВсего подключений: %d\n\n", connectedClientCount);
            sigHupReceived = 0;
        }

        // инициализация набора файловых дескрипторов для мониторинга
        fd_set readReadyDescriptors;
        FD_ZERO(&readReadyDescriptors);
        FD_SET(listeningSocketDescriptor, &readReadyDescriptors);
        int maxFileDescriptor = listeningSocketDescriptor;

        // добавление дескрипторов активных клиентов
        for (int connectionIndex = 0; connectionIndex < connectedClientCount; connectionIndex++)
        {
            int clientSocket = activeConnections[connectionIndex].connectionSocket;
            FD_SET(clientSocket, &readReadyDescriptors);
            if (clientSocket > maxFileDescriptor)
            {
                maxFileDescriptor = clientSocket;
            }
        }

        // ожидание активности на сокетах с поддержкой сигналов
        if (pselect(maxFileDescriptor + 1, &readReadyDescriptors, NULL, NULL, NULL, &originalSignalMask) == -1)
        {
            if (errno == EINTR) continue;  // продолжить при прерывании сигналом
            return -1;
        }

        // обработка нового входящего подключения
        if (FD_ISSET(listeningSocketDescriptor, &readReadyDescriptors) &&
            connectedClientCount < MAX_CONCURRENT_CLIENTS)
        {
            ClientConnection* newConnection = &activeConnections[connectedClientCount];
            socklen_t addressLength = sizeof(newConnection->clientAddress);
            int newClientSocket = accept(listeningSocketDescriptor,
                (struct sockaddr*)&newConnection->clientAddress,
                &addressLength);
            if (newClientSocket >= 0)
            {
                newConnection->connectionSocket = newClientSocket;
                connectedClientCount++;
                printf("The new connection has been accepted. Total clients: %d\n", connectedClientCount);
            }
            else
            {
                perror("Error when accepting the connection!!!");
            }
        }

        // чтение данных от подключенных клиентов
        for (int connectionIndex = 0; connectionIndex < connectedClientCount; connectionIndex++)
        {
            ClientConnection* currentConnection = &activeConnections[connectionIndex];
            if (FD_ISSET(currentConnection->connectionSocket, &readReadyDescriptors))
            {
                int receivedBytes = read(currentConnection->connectionSocket,
                    messageDataBuffer,
                    sizeof(messageDataBuffer) - 1);
                if (receivedBytes > 0)
                {
                    messageDataBuffer[receivedBytes] = '\0';
                    printf("Message received: %s\n", messageDataBuffer);
                }
                else
                {
                    // закрытие соединения при ошибке или разрыве
                    close(currentConnection->connectionSocket);
                    printf("The connection to the client is closed.\n");

                    // удаление клиента из массива (заменяем последним элементом)
                    activeConnections[connectionIndex] = activeConnections[connectedClientCount - 1];
                    connectedClientCount--;
                    connectionIndex--;
                }
            }
        }
    }
}

int main()
{
    int serverListeningSocket = createListeningSocket(2523);
    printf("The server is running and waiting for connections on port 2523...\n");

    sigset_t savedSignalMask;
    setupSignalHandling(&savedSignalMask);

    int operationResult = processClientConnections(serverListeningSocket, savedSignalMask);
    if (operationResult == -1)
    {
        perror("Critical error in pselect operation!!!");
        close(serverListeningSocket);
        return EXIT_FAILURE;
    }
    close(serverListeningSocket);
    return EXIT_SUCCESS;
}