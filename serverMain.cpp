#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <random>
using namespace std;

struct Probability {
    int minn = 0, maxn = 0;
};

const int BUFFER_SIZE = 1024;
const string PRINTLINE = "---------------------------";
const map<string, Probability> CARD_POOL = {
    {"Shovel", Probability{.minn = 67, .maxn = 100}},
    {"Double", Probability{.minn = 45, .maxn = 67}},
    {"MetalDetector", Probability{.minn = 24, .maxn = 45}},
    {"MedKit", Probability{.minn = 12, .maxn = 24}},
    {"FryingPan", Probability{.minn = 0, .maxn = 12}}
};

struct PlayerInfo {
    int health = 5, score = 0;
    int sufferedDamage = 0;
    int causedDamage = 0;
    vector<string> backpack = {"Shovel", "Double"};
    bool skipped = false;
};
struct ClientInfo {
    sockaddr_in addr;
    string name;
    PlayerInfo pInfo;
};
map<string, ClientInfo> clients;
mutex clientsMutex;
enum SearchedResultType {
    FOUND_NAME = 1,
    FOUND_KEY = 2,
    NOT_FOUND = 0
};
struct ClientCheckedInfo {
    int checkedType;
    string result, key;
}; ClientCheckedInfo searchForClient(const sockaddr_in &clientAddr, const string &name) {
    auto it = clients.find(name);
    if (it != clients.end()) {
        return ClientCheckedInfo{.checkedType = FOUND_NAME, .result = name};
    }

    string key, pName;
    for (auto &p : clients) {
        if (p.second.addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr) {
            key = p.first;
            pName = p.second.name;
            break;
        }
    }
    if (! key.empty()) {
        return ClientCheckedInfo{.checkedType = FOUND_KEY, .result = pName, .key = key};
    }
    return ClientCheckedInfo{.checkedType = NOT_FOUND};
}
void sendMessage(SOCKET serverSock, const string &msg, const sockaddr &clientAddr) {
    sendto(serverSock, msg.c_str(), msg.size(), 0, &clientAddr, sizeof(clientAddr));
}
void sendMessage(SOCKET serverSock, const string &msg, const sockaddr_in &clientAddr) {
    sendto(serverSock, msg.c_str(), msg.size(), 0, (sockaddr *) &clientAddr, sizeof(clientAddr));
}
void broadcastToClient(SOCKET serverSock, const string &msg, const string &name = "") {
    for (auto &p : clients) {
        if (p.first != name) sendMessage(serverSock, msg, p.second.addr);
    }
}

int hasItem(const vector<string> &backpack, const string &item) {
    for (int i = 0; i < backpack.size(); i++) {
        if (backpack[i] == item) return i;
    }
    return -1;
}
void eraseItem(vector<string> &backpack, const string &item) {
    int pos = hasItem(backpack, item);
    if (pos >= 0) backpack.erase(backpack.begin() + pos);
}

random_device soilDevice;
mt19937 soilGen(soilDevice());
uniform_int_distribution<> soilDistribution(0, 1);
struct Soil {
    int damage = 0;
    bool doubled = false;
    string owner;
} soil[16];
Soil generateSoil() {
    return Soil{.damage = soilDistribution(soilGen)};
}
void initSoil() {
    for (int i = 0; i < 8; i++) {
        soil[i] = generateSoil();
    }
}

bool inGame = false;

void flipSoil(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr) {
    Soil s = soil[0];
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    } else if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "Shovel") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 Shovel 翻开土块";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }

        string sentMsg;
        if (s.damage) {
            pInfo.sufferedDamage += s.damage;
            pInfo.health -= s.damage;

            if (s.doubled && s.owner != cci.result) {
                pInfo.causedDamage += s.damage;
            }
            sentMsg = "SYS[#] 哎呀坏了坏了，是炸弹啊我去\n> [#] 炸弹对 ";
            sentMsg += cci.result + " 造成了 " + to_string(s.damage) + " 点伤害";
        } else {
            sentMsg = "SYS[#] 土块里什么都没有，无事发生捏";
        }
        broadcastToClient(serverSock, sentMsg);
        cout << "[#] " << cci.result << " 翻开了土块" << endl;
        eraseItem(pInfo.backpack, "Shovel");
        clients[res].pInfo = pInfo;

        for (int i = 0; i < 7; i ++) {
            soil[i] = soil[i+1];
        }
        soil[7] = generateSoil();
    }
}
void drawCard(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    } else if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> disSum(1, 2);
        uniform_int_distribution<> disCard(0, 100);
        int sum = disSum(gen);
        string cardMsg;
        for (int _ = 0; _ < sum; _++) {
            string cRes = "Shovel";
            int pRand = disCard(gen);
            for (auto &p : CARD_POOL) {
                if (pRand < p.second.maxn && pRand >= p.second.minn) {
                    cRes = p.first;
                    break;
                }
            }
            pInfo.backpack.push_back(cRes);
            cardMsg += cRes + " ";
        }
        broadcastToClient(serverSock, "SYS[@] " + cci.result + " 获得了 " + cardMsg);
        clients[res].pInfo = pInfo;
    }
}

bool initWinsock() {
    WSADATA wsaData; int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return result == 0;
}

SOCKET startUdpServer(unsigned short port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

string getClientKey(const sockaddr_in &clientAddr) {
    return string(inet_ntoa(clientAddr.sin_addr)) + ":" + to_string(ntohs(clientAddr.sin_port));
}
void handleMessage(SOCKET serverSock, const string &message, const sockaddr_in &clientAddr);
void receiveMessage(SOCKET serverSock) {
    char buffer[BUFFER_SIZE];
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int recvLen = recvfrom(serverSock, buffer, BUFFER_SIZE - 1, 0, (sockaddr *) &clientAddr, &addrLen);
        if (recvLen > 0) {
            buffer[recvLen] = '\0'; string message(buffer);
            handleMessage(serverSock, message, clientAddr);
        }
    }
}

bool command(const string& message, const string& command) {
    return message.substr(0, command.size()) == command;
}

void handleMessage(SOCKET serverSock, const string &message, const sockaddr_in &clientAddr) {
    string key = getClientKey(clientAddr);
    string clientIp = inet_ntoa(clientAddr.sin_addr);
    int clientPort = ntohs(clientAddr.sin_port);

    lock_guard lock(clientsMutex);
    if (command(message, "INSERT")) {
        string playerName = message.substr(6);
        if (inGame) {
            cout << "[#] " << playerName << " 试图加入游戏，但被土块吃掉了" << endl;
            sendMessage(serverSock, "REFUSE", clientAddr);
            return;
        }
        bool found = false;
        string legacy;
        for (auto &p : clients) {
            if (p.second.name == playerName) {
                found = true;
                legacy = p.first;
                break;
            }
        }
        if (found) {
            clients[legacy].addr = clientAddr;
        } else {
            clients[playerName] = {clientAddr, playerName};
            string sentMsg = "[#] 玩家 " + playerName + " 插入了游戏！";
            cout << sentMsg << endl;
            broadcastToClient(serverSock, "SYS" + sentMsg, playerName);
            cout << "> [@] 还有 " << clients.size() << " 个玩家" << endl;
            sentMsg = "[#] 你的端口号: " + clientIp + ":" + to_string(clientPort);
            sendMessage(serverSock, sentMsg, clientAddr);
        }
    }
    if (command(message, "QUIT")) {
        string playerName = message.substr(4);
        ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
        if (cci.checkedType == FOUND_NAME) {
            clients.erase(cci.result);
            string sentMsg = "[#] 玩家 " + cci.result + " 离开了游戏";
            cout << sentMsg << endl;
            broadcastToClient(serverSock, "SYS" + sentMsg);
        } else if (cci.checkedType == FOUND_KEY) {
            clients.erase(cci.key);
            string sentMsg = "[#] 玩家 " + cci.result + " 离开了游戏";
            cout << sentMsg << endl;
            broadcastToClient(serverSock, "SYS" + sentMsg);
        }

        if (cci.checkedType != NOT_FOUND) cout << "> [@] 还有 " << clients.size() << " 个玩家" << endl;
    }
    if (command(message, "CHAT")) {
        int msgLen = stoi(message.substr(4, 2));
        string msg = message.substr(6, msgLen);
        string playerName = message.substr(msgLen + 6);
        string sentMsg = "[@] " + playerName + " : " + msg;
        cout << sentMsg << endl;
        broadcastToClient(serverSock, "SYS" + sentMsg);
    }
    if (command(message, "SHOWINFO")) {
        if (inGame) {
            string playerName = message.substr(8);
            ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
            PlayerInfo pInfo;
            if (cci.checkedType == FOUND_NAME) {
                pInfo = clients[cci.result].pInfo;
            } else if (cci.checkedType == FOUND_KEY) {
                pInfo = clients[cci.key].pInfo;
            }

            if (cci.checkedType != NOT_FOUND) {
                string name = cci.result;
                string sentMsg = name + " : ";
                if (pInfo.health <= 0) sentMsg += "跟土块爆了";
                else {
                    sentMsg += "还有 " + to_string(pInfo.health) + " 点生命 \n";
                    if (! pInfo.backpack.empty()) {
                        sentMsg += "> [@] 你还有手牌 ";
                        for (auto& s : pInfo.backpack) sentMsg += s + " ";
                    } else {
                        sentMsg += "> [@] 你已经什么手牌都没有了";
                    }
                }

                cout << "[#] " << name << " 查询了自己的信息" << endl;
                sentMsg = "SYS[@] " + sentMsg;
                sendMessage(serverSock, sentMsg, clientAddr);
            }
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "FLIP")) {
        if (inGame) {
            string playerName = message.substr(4);
            flipSoil(serverSock, playerName, clientAddr);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "DRAW")) {
        if (inGame) {
            string playerName = message.substr(4);
            drawCard(serverSock, playerName, clientAddr);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
}

int main() {
    srand(time(NULL));

    if (!initWinsock()) {
        cout << "你的Winsock好像只剩下8.6（有1.4了诶";
        return -1;
    }
    cout << "输入服务端的端口，是... ";
    unsigned short port; cin >> port;
    cout << endl;
    SOCKET serverSock = startUdpServer(port);
    if (serverSock == INVALID_SOCKET) {
        cout << "你的服务端Socket似乎跟土块爆了，也许是其他问题导致的";
        WSACleanup();
        return -1;
    }
    cout << "启动！ServerSocket的端口号是 " << port << endl;
    cout << "启动之后，一定要输入操作符 #quit 以确保正确的登出啊 UvU" << endl;

    thread t(receiveMessage, serverSock);
    t.detach();
    while (true) {
        string opt; cin >> opt;
        if (opt[0] == '#') {
            string command = opt.substr(1);
            if (command == "quit") break;
            if (command == "start" && ! inGame) {
                cout << "[@] 游戏开始！" << endl;
                broadcastToClient(serverSock, "SYS[#] 游戏开始！\n" + PRINTLINE);
                initSoil();

                random_device rd;
                mt19937 gen(rd());
                uniform_int_distribution<> disCard(0, 100);
                int cardNum = max(2, (int) (clients.size() / 2 + 1));
                for (auto &p : clients) {
                    for (int _ = 0; _ < cardNum; _ ++) {
                        string cRes = "Shovel";
                        int pRand = disCard(gen);
                        for (auto &g : CARD_POOL) {
                            if (pRand < g.second.maxn && pRand >= g.second.minn) {
                                cRes = g.first;
                                break;
                            }
                        }

                        p.second.pInfo.backpack.push_back(cRes);
                    }

                    string sentMsg = p.second.name + " : 还有 " + to_string(p.second.pInfo.health) + " 点体力 \n";
                    sentMsg += "> [@] 你还有手牌 ";
                    for (auto &s : p.second.pInfo.backpack) sentMsg += s + " ";
                    sentMsg += "\n" + PRINTLINE;
                    sentMsg = "SYS[@] " + sentMsg;
                    sendMessage(serverSock, sentMsg, p.second.addr);
                    inGame = true;
                }
            }
        }
    }
    closesocket(serverSock);
    WSACleanup();
    return 0;
}