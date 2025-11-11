#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <system_error>
#include <csignal>
#include <cstring>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>

class Client {
public:
    int clientSocket;
    struct sockaddr_in clientAddr;
    
    Client(int socket, const struct sockaddr_in& addr) 
        : clientSocket(socket), clientAddr(addr) {}
    
    ~Client() {
        if (clientSocket >= 0) {
            close(clientSocket);
        }
    }
    
    // Запрещаем копирование
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    
    // Разрешаем перемещение
    Client(Client&& other) noexcept 
        : clientSocket(other.clientSocket), clientAddr(other.clientAddr) {
        other.clientSocket = -1;
    }
    
    Client& operator=(Client&& other) noexcept {
        if (this != &other) {
            if (clientSocket >= 0) {
                close(clientSocket);
            }
            clientSocket = other.clientSocket;
            clientAddr = other.clientAddr;
            other.clientSocket = -1;
        }
        return *this;
    }
};

class Server {
private:
    static const int CLIENT_LIMIT = 2;
    static inline volatile sig_atomic_t signalFlag = 0;
    
    int serverSocket;
    std::vector<Client> clients;
    sigset_t prevSignalMask;
    
public:
    Server() : serverSocket(-1) {
        configureSignalHandler();
    }
    
    ~Server() {
        if (serverSocket >= 0) {
            close(serverSocket);
        }
    }
    
    // Запрещаем копирование и перемещение
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

private:
    static void handleSigHup(int signal) {
        signalFlag = 1;
    }
    
    void configureSignalHandler() {
        struct sigaction sa;
        sigaction(SIGHUP, NULL, &sa);
        sa.sa_handler = handleSigHup;
        sa.sa_flags |= SA_RESTART; 
        sigaction(SIGHUP, &sa, NULL);
        
        sigset_t blockedMask;
        sigemptyset(&blockedMask);
        sigaddset(&blockedMask, SIGHUP);
        sigprocmask(SIG_BLOCK, &blockedMask, &prevSignalMask);
    }
    
    void initializeServer(int port) {
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            throw std::system_error(errno, std::system_category(), "Ошибка создания сокета");
        }
        
        // Установка опции для повторного использования адреса
        int opt = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(serverSocket);
            throw std::system_error(errno, std::system_category(), "Ошибка установки SO_REUSEADDR");
        }
        
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(port);
        
        if (bind(serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) != 0) {
            close(serverSocket);
            throw std::system_error(errno, std::system_category(), "Ошибка привязки сокета");
        }
        
        if (listen(serverSocket, CLIENT_LIMIT) != 0) {
            close(serverSocket);
            throw std::system_error(errno, std::system_category(), "Ошибка при прослушивании сокета");
        }
    }
    
    void handleNewConnection() {
        if (clients.size() >= CLIENT_LIMIT) {
            std::cout << "Достигнут лимит клиентов (" << CLIENT_LIMIT << ")" << std::endl;
            return;
        }
        
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        memset(&clientAddr, 0, sizeof(clientAddr));
        
        int newSocket = accept(serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
        if (newSocket >= 0) {
            clients.emplace_back(newSocket, clientAddr);
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "Новый клиент подключен: " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;
        } else {
            std::cerr << "Ошибка accept: " << strerror(errno) << std::endl;
        }
    }
    
    void handleClientData(Client& client) {
        char messageBuffer[1024] = {0};
        int bytesRead = read(client.clientSocket, messageBuffer, sizeof(messageBuffer) - 1);
        
        if (bytesRead > 0) {
            messageBuffer[bytesRead] = '\0';
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client.clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "Сообщение от " << clientIP << ": " << messageBuffer;
        } else {
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client.clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "Соединение закрыто клиентом: " << clientIP << std::endl;
            client.clientSocket = -1; // Помечаем для удаления
        }
    }
    
    void removeDisconnectedClients() {
        auto it = clients.begin();
        while (it != clients.end()) {
            if (it->clientSocket < 0) {
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void printClientsList() {
        std::cout << "Подключенные клиенты (" << clients.size() << "):" << std::endl;
        for (size_t i = 0; i < clients.size(); ++i) {
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clients[i].clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "  " << (i + 1) << ". " << clientIP << ":" << ntohs(clients[i].clientAddr.sin_port) << std::endl;
        }
        std::cout << std::endl;
    }
    
    void handleServerConnections() {
        std::cout << "Сервер запущен, ожидание подключений..." << std::endl;
        
        while (true) {
            // Обработка сигнала SIGHUP
            if (signalFlag) {
                printClientsList();
                signalFlag = 0;
            }
            
            // Подготовка дескрипторов для select
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(serverSocket, &readfds);
            int maxFd = serverSocket;
            
            // Добавляем дескрипторы клиентов в набор
            for (const auto& client : clients) {
                FD_SET(client.clientSocket, &readfds);
                if (client.clientSocket > maxFd) {
                    maxFd = client.clientSocket;
                }
            }
            
            // Ожидание событий с использованием pselect
            if (pselect(maxFd + 1, &readfds, nullptr, nullptr, nullptr, &prevSignalMask) == -1) {
                if (errno == EINTR) continue;  // Игнорируем прерывания от сигналов
                throw std::system_error(errno, std::system_category(), "Ошибка работы с pselect");
            }
            
            // Обработка нового подключения клиента
            if (FD_ISSET(serverSocket, &readfds)) {
                handleNewConnection();
            }
            
            // Чтение данных от клиентов
            for (auto& client : clients) {
                if (FD_ISSET(client.clientSocket, &readfds)) {
                    handleClientData(client);
                }
            }
            
            // Удаляем отключенных клиентов
            removeDisconnectedClients();
        }
    }
    
public:
    void run(int port) {
        try {
            initializeServer(port);
            handleServerConnections();
        } catch (const std::system_error& e) {
            std::cerr << "Системная ошибка: " << e.what() << " (код: " << e.code() << ")" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Ошибка: " << e.what() << std::endl;
        }
    }
};

int main() {
    Server server;
    server.run(2523);
    return 0;
}
