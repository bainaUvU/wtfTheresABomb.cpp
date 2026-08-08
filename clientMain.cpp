#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <thread>
#include <iostream>
using namespace std;

const int CONNECT_TIMEOUT = 16;
const int BUFFER_SIZE = 1024;

bool initWinsock() {
    WSADATA wsaData; int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return result == 0;
}

sockaddr_in serverAddr;
string playerName;

bool sendMessage(SOCKET clientSock, string type, string msg) {
    string sentMsg = type + msg;
    int sent = sendto(clientSock, sentMsg.c_str(), sentMsg.length(), 0, (SOCKADDR *) &serverAddr, sizeof(serverAddr));
    return sent == SOCKET_ERROR;
}
bool connectToServer(SOCKET clientSock, string serverIp, unsigned int port) {
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());

    if (inet_addr(serverIp.c_str()) == INADDR_NONE) {
        cout << "你貌似输入了无效的IP地址" << endl;
        return false;
    }

    cout << "输入玩家的名字... ";
    cin >> playerName; cout << endl;
    playerName = playerName.substr(0, 64);
    if (sendMessage(clientSock, "INSERT", playerName)) {
        cout << "你的连接貌似跟土块爆了，发送给服务端的测试消息没有成功被接收" << endl;
        return false;
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int recvLen = recv(clientSock, buffer, BUFFER_SIZE - 1, 0);
    if (recvLen > 0) {
        buffer[recvLen] = '\0';
        string msg(buffer);
        if (msg == "REFUSE") {
            cout << "啊哦，游戏已经开始了 你不能再插入游戏了" << endl;
            return false;
        }
        cout << "好耶！连接上了服务端 " << serverIp << ":" << port << endl;
        cout << buffer << endl;
        return true;
    }
    return false;
}
void handleMessage(const string &message);
void receiveMessage(SOCKET clientSock) {
    char buffer[BUFFER_SIZE];
    sockaddr_in sourceAddr;
    int addrLen = sizeof(sourceAddr);
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int recvLen = recvfrom(clientSock, buffer, BUFFER_SIZE - 1, 0, (sockaddr *) &sourceAddr, &addrLen);
        if (recvLen > 0) {
            buffer[recvLen] = '\0';
            string msg(buffer);
            handleMessage(msg);
        }
    }
}
void handleMessage(const string &message) {
    if (message.substr(0, 3) == "SYS") cout << message.substr(3) << endl;
}

int main() {
    if (!initWinsock()) {
        cout << "你的Winsock好像只剩下8.6（有1.4了诶";
        return -1;
    }
    SOCKET clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSock == INVALID_SOCKET) {
        cout << "你的客户端Socket似乎跟土块爆了，也许是其他问题导致的";
        WSACleanup();
        return -1;
    }
    cout << "输入服务端的IP地址... ";
    string serverIp; cin >> serverIp;
    cout << "输入服务端的端口号... ";
    int port; cin >> port;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (char*) &CONNECT_TIMEOUT, sizeof(CONNECT_TIMEOUT));
    if (!connectToServer(clientSock, serverIp, port)) {
        cout << "你并没有连接上关于 " << serverIp << ":" << port << " 的服务端" << endl;
        cout << "可能是服务端爆炸了捏 QwQ";
        closesocket(clientSock);
        WSACleanup();
        return -1;
    }
    cout << "连接上服务端之后，一定要输入操作符 #quit 以确保正确的登出啊 UvU" << endl;

    thread t(receiveMessage, clientSock);
    t.detach();
    while (true) {
        string opt; cin >> opt;
        if (opt[0] == '#') {
            string command = opt.substr(1);
            if (command == "quit") {
                sendMessage(clientSock, "QUIT", playerName);
                break;
            }
            if (command == "showInfo") sendMessage(clientSock, "SHOWINFO", playerName);
            if (command == "flip") sendMessage(clientSock, "FLIP", playerName);
            if (command == "draw") sendMessage(clientSock, "DRAW", playerName);
            if (command == "double") {
                int pos; cin >> pos;
                sendMessage(clientSock, "DOUBLE", (char) (pos + '0') + playerName);
            }
            if (command == "medkit") sendMessage(clientSock, "MEDKIT", playerName);
            if (command == "fryingPan") sendMessage(clientSock, "PAN", playerName);
            if (command == "magnifier") {
                string bName; cin >> bName;
                int length = bName.size();
                string nameLen = "00";
                if (length < 10) nameLen[1] = length + '0';
                else nameLen = to_string(length);
                sendMessage(clientSock, "MAGNIFIER", nameLen + bName + playerName);
            }
            if (command == "hook") {
                string bName; cin >> bName;
                int length;
                length = bName.size();
                string nameLen = "00";
                if (length < 10) nameLen[1] = length + '0';
                else nameLen = to_string(length);

                string card; cin >> card;
                length = card.size();
                string cardLen = "00";
                if (length < 10) cardLen[1] = length + '0';
                else cardLen = to_string(length);

                sendMessage(clientSock, "HOOK", nameLen + bName + cardLen + card + playerName);
            }
            if (command == "bomb") {
                int pos; cin >> pos;
                sendMessage(clientSock, "BOMB", (char) (pos + '0') + playerName);
            }
        } else {
            int length = min(64, (int) opt.size());
            string msgLen = "00";
            if (length < 10) msgLen[1] = length + '0';
            else msgLen = to_string(length);
            sendMessage(clientSock, "CHAT", msgLen + opt.substr(0, 64) + playerName);
        }
    }
    closesocket(clientSock);
    WSACleanup();
    return 0;
}