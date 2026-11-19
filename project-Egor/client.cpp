// =====================================================================
//  client.cpp — ШАБЛОН TCP-КЛИЕНТА (зачётный проект)
//
//  Что делает: подключается к серверу, печатает приветствие сервера,
//  затем в цикле читает команду с клавиатуры, отправляет её серверу
//  и печатает ответ. Команда QUIT завершает работу.
//
//  Запуск:
//    client                 — подключиться к серверу на этом же компьютере
//    client 192.168.1.15    — подключиться к серверу на другом компьютере
//
//  Сборка:
//    Windows, MinGW / Code::Blocks:  g++ client.cpp -o client.exe -lws2_32
//    Windows, Visual Studio:         просто собрать проект
//    Linux / macOS:                  g++ client.cpp -o client
//
//  Совет: команды вводите латиницей. Русский ввод в консоли Windows
//  часто ломает кодировку.
// =====================================================================

// ---------------------------------------------------------------------
//  Подключение сетевых библиотек (то же самое, что в сервере).
//  Этот блок менять не нужно.
// ---------------------------------------------------------------------
#ifdef _WIN32
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601        // Windows 7+: нужно старому MinGW для inet_pton/inet_ntop
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>              // inet_pton
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #define SEND_FLAGS 0
#else
    #include <sys/socket.h>            // socket, connect, send, recv
    #include <netinet/in.h>            // struct sockaddr_in, htons
    #include <arpa/inet.h>             // inet_pton
    #include <unistd.h>                // close
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket close
    #define SEND_FLAGS MSG_NOSIGNAL
#endif

#include <iostream>
#include <string>

using namespace std;

const int PORT = 9000;   // порт сервера — должен совпадать с сервером

// Отправить строку с '\n' в конце. Цикл нужен, потому что send()
// может отправить только часть байтов (подробно — в server.cpp).
bool sendLine(SOCKET s, const string& text)
{
    string data = text + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.c_str() + sent, (int)(data.size() - sent), SEND_FLAGS);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Принять одну строку до '\n'. TCP — это поток байтов без границ
// сообщений, поэтому границу ищем сами (подробно — в server.cpp).
bool recvLine(SOCKET s, string& line)
{
    line.clear();
    char c;
    while (true) {
        int n = recv(s, &c, 1, 0);
        if (n <= 0) return false;   // 0 — сервер закрыл соединение, -1 — ошибка
        if (c == '\n') return true;
        if (c != '\r') line += c;
    }
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    WSADATA wsa;                     // в Windows — обязательно включить Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << "WSAStartup failed" << endl;
        return 1;
    }
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Адрес сервера: из командной строки или 127.0.0.1 (localhost —
    // «этот же компьютер»; данные не уходят в сеть, но проходят через
    // весь сетевой стек ОС, как настоящий трафик).
    string serverIp = "127.0.0.1";
    if (argc > 1) serverIp = argv[1];

    // 1. socket() — создаём TCP-сокет (IPv4 + поток), как и в сервере.
    //    Клиенту НЕ нужны bind() и listen(): свободный порт для клиента
    //    система выберет сама при connect() (эфемерный порт).
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cout << "socket() error" << endl;
        return 1;
    }

    // 2. Заполняем адрес сервера.
    //    htons() — порт в сетевой порядок байтов;
    //    inet_pton() — переводит текст "127.0.0.1" в 4 байта адреса.
    //    Возвращает 1, если адрес записан правильно.
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, serverIp.c_str(), &addr.sin_addr) != 1) {
        cout << "Wrong IP address: " << serverIp << endl;
        closesocket(sock);
        return 1;
    }

    // 3. connect() — подключаемся. Здесь происходит тройное рукопожатие
    //    TCP (SYN, SYN-ACK, ACK). Если сервер не запущен, connect()
    //    вернёт ошибку «соединение отклонено».
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cout << "Cannot connect to " << serverIp << ":" << PORT
             << " (is the server running?)" << endl;
        closesocket(sock);
        return 1;
    }
    cout << "Connected to " << serverIp << ":" << PORT << endl;

    // 4. Сначала сервер присылает приветствие — читаем и печатаем его.
    string answer;
    if (!recvLine(sock, answer)) {
        cout << "Server closed the connection" << endl;
        closesocket(sock);
        return 1;
    }
    cout << "Server: " << answer << endl;

    // 5. Главный цикл: команда с клавиатуры -> серверу -> ответ на экран.
    string command;
    while (true) {
        cout << "> ";
        if (!getline(cin, command)) break;   // конец ввода (Ctrl+Z / Ctrl+D)
        if (command.empty()) continue;       // пустые строки не отправляем

        if (!sendLine(sock, command)) {
            cout << "Send error: connection lost" << endl;
            break;
        }
        if (!recvLine(sock, answer)) {
            cout << "Server closed the connection" << endl;
            break;
        }
        cout << "Server: " << answer << endl;

        if (command == "QUIT") break;        // сервер ответил BYE — выходим
    }

    // 6. Закрываем соединение. Сервер увидит, что recv() вернул 0.
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
