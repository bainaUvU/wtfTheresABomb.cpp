#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
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
    {"Shovel", Probability{.minn = 82, .maxn = 100}},
    {"Double", Probability{.minn = 60, .maxn = 82}},
    {"MetalDetector", Probability{.minn = 39, .maxn = 60}},
    {"MedKit", Probability{.minn = 27, .maxn = 39}},
    {"FryingPan", Probability{.minn = 17, .maxn = 27}},
    {"Magnifier", Probability{.minn = 11, .maxn = 17}},
    {"FishingHook", Probability{.minn = 5, .maxn = 11}},
    {"Bomb", Probability{.minn = 0, .maxn = 5}},
};

const string AVAILABLE_CARDS[] = {"Shovel", "Double", "MetalDetector", "MedKit", "FryingPan", "Magnifier", "FishingHook", "Bomb", "-1"};

struct PlayerInfo {
    int health = 5, score = 0;
    int sufferedDamage = 0;
    int causedDamage = 0;
    int damageReduction = 0;
    vector<string> backpack = {"Shovel", "Double"};
};
struct ClientInfo {
    sockaddr_in addr;
    string name;
    PlayerInfo pInfo;
};
bool compareP (pair<string, ClientInfo> a, pair<string, ClientInfo> b) {
    if (a.second.pInfo.score == b.second.pInfo.score) return a.first < b.first;
    return a.second.pInfo.score > b.second.pInfo.score;
}

map<string, ClientInfo> clients;
mutex clientsMutex;
mutex gameMutex;

string getClientKey(const sockaddr_in &clientAddr) {
    return string(inet_ntoa(clientAddr.sin_addr)) + ":" + to_string(ntohs(clientAddr.sin_port));
}

enum SearchedResultType {
    FOUND_NAME = 1,
    FOUND_KEY = 2,
    NOT_FOUND = 0
};
struct ClientCheckedInfo {
    int checkedType;
    string result, key;
};
ClientCheckedInfo searchForClient(const sockaddr_in &clientAddr, const string &name) {
    auto it = clients.find(name);
    if (it != clients.end()) {
        return ClientCheckedInfo{.checkedType = FOUND_NAME, .result = name};
    }

    string fullKey = getClientKey(clientAddr);
    for (auto &p : clients) {
        if (p.first.empty()) continue;
        if (getClientKey(p.second.addr) == fullKey) {
            return ClientCheckedInfo{.checkedType = FOUND_KEY, .result = p.second.name, .key = p.first};
        }
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
} soil[9];
Soil generateSoil() {
    return Soil{.damage = soilDistribution(soilGen)};
}
void initSoil() {
    for (int i = 0; i < 8; i++) {
        soil[i] = generateSoil();
    }
}

bool inGame = false, doSkip = false, finished = false;
int plrInd, turns, causeOfD = -1, plrN;
vector<pair<string, string>> shuffledP;

enum CauseOfDeath {
    BOMB = 238
};

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
            if (pInfo.damageReduction) {
                if (pInfo.damageReduction > s.damage) pInfo.damageReduction -= s.damage;
                else {
                    pInfo.health -= s.damage - pInfo.damageReduction;
                    pInfo.damageReduction = 0;
                }
            } else pInfo.health -= s.damage;

            if (s.doubled && s.owner != cci.result) {
                pInfo.causedDamage += s.damage;
            }
            sentMsg = "SYS[#] 哎呀坏了坏了，是炸弹啊我去\n> [#] 炸弹对 ";
            sentMsg += cci.result + " 造成了 " + to_string(s.damage) + " 点伤害";
            causeOfD = -1;
            if (pInfo.health <= 0) {
                causeOfD = BOMB;
            }
            broadcastToClient(serverSock, "SOUNDexplosion");
        } else {
            sentMsg = "SYS[#] 土块里什么都没有，无事发生捏";
            broadcastToClient(serverSock, "SOUNDflip");
        }
        broadcastToClient(serverSock, sentMsg);
        cout << "[#] " << cci.result << " 翻开了土块" << endl;
        eraseItem(pInfo.backpack, "Shovel");
        clients[res].pInfo = pInfo;

        for (int i = 0; i < 7; i ++) {
            soil[i] = soil[i+1];
        }
        soil[7] = generateSoil();
        doSkip = true;
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
        broadcastToClient(serverSock, "SOUNDdrawCard");
        cout << "[#] " << cci.result << " 做了一次抽牌" << endl;
        broadcastToClient(serverSock, "SYS[@] " + cci.result + " 获得了 " + cardMsg);
        clients[res].pInfo = pInfo;
        doSkip = true;
    }
}
void doubleBomb(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr, const short pos) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    }
    if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "Double") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 Double 翻倍土块里的炸弹";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        if (pos < 0 || pos > 7) {
            string sentMsg = "SYS[#] 这个位置不合理吧...";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        cout << "[#] " << cci.result << " 翻倍了土块 " << (char) (pos + '0') << " 里的炸弹" << endl;
        eraseItem(pInfo.backpack, "Double");
        clients[cci.result].pInfo = pInfo;
        if (soil[pos].damage >= 16) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> disSum(1, 2);
            if (disSum(gen) == 1) {
                broadcastToClient(serverSock, "SYS[@] 哎呀坏了坏了，“炸膛”了！" + cci.result + " 收到了 1 点伤害！");
                soil[pos].damage -= 1;
                pInfo.health -= 1;
                pInfo.sufferedDamage += 1;
                clients[cci.result].pInfo = pInfo;
                broadcastToClient(serverSock, "SOUNDexplosion");
                doSkip = true;

                causeOfD = -1;
                if (pInfo.health <= 0) {
                    causeOfD = BOMB;
                }
                return;
            }
        }
        broadcastToClient(serverSock, "SOUNDbitSound");
        soil[pos].damage *= 2;
        soil[pos].doubled = true;
        soil[pos].owner = cci.result;

        broadcastToClient(serverSock, "SYS[@] " + cci.result + " 翻倍了土块 " + (char) (pos + '0') + " 里的炸弹！");
        clients[res].pInfo = pInfo;
        doSkip = true;
    }
}
void metalDetect(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    }
    if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "MetalDetector") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 MetalDetector 勘探土块";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        random_device rd;
        mt19937 gen(rd()), genP(rd());
        uniform_int_distribution<> disSum(0, 3), disProb(1, 8);
        int pos = disSum(gen), glitch = disProb(genP);
        Soil s = soil[pos]; int damage = s.damage;
        if (damage > 0 && glitch == 1) damage = 0;
        else if (damage == 0 && glitch == 1) damage = 1;

        cout << "[#] " << cci.result << " 使用了探测仪" << endl;
        string sentMsg = "SYS[@] 探测仪偷偷告诉你：土块 " + to_string(pos) + " 里";
        if (damage > 0) sentMsg += "有 " + to_string(damage) + " 颗炸弹";
        else sentMsg += "什么也没有";
        sendMessage(serverSock, sentMsg, clientAddr);
        eraseItem(pInfo.backpack, "MetalDetector");
        clients[res].pInfo = pInfo;
        sendMessage(serverSock, "SOUNDbitSound", clientAddr);
    }
}
void suspect(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr, const string &recipient) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    }
    if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "Magnifier") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 Magnifier 来窥探别人的手牌";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        if (clients.find(recipient) == clients.end()) {
            string sentMsg = "SYS[#] 好像没有玩家 " + recipient;
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        string sentMsg = "SYS[@] 偷偷告诉你：" + recipient + " 含有手牌 ";
        for (auto &p : clients[recipient].pInfo.backpack) {
            sentMsg += p + " ";
        }

        cout << "[#] " << cci.result << " 使用了放大镜窥视 " << recipient << " 的手牌" << endl;
        sendMessage(serverSock, sentMsg, clientAddr);
        eraseItem(pInfo.backpack, "Magnifier");
        clients[res].pInfo = pInfo;
        sendMessage(serverSock, "SOUNDbitSound", clientAddr);
    }
}
void getchItem(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr, const string &recipient, const string &item) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    }
    if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "FishingHook") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 FishingHook 勾取物品";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        if (clients.find(recipient) == clients.end()) {
            string sentMsg = "SYS[#] 好像没有玩家 " + recipient;
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        if (item == "FishingHook") {
            string sentMsg = "SYS[#] 为什么要做这种没意义的操作诶";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        bool byp = true;
        int p = 0;
        while (AVAILABLE_CARDS[p] != "-1") {
            if (AVAILABLE_CARDS[p ++] == item) {
                byp = false;
                break;
            }
        }
        if (byp == false || item == "-1") {
            string sentMsg = "SYS[#] 这个真的是一张牌吗？";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        PlayerInfo rPInfo = clients[recipient].pInfo;
        cout << "[#] " << cci.result << " 使用了鱼钩试图偷走 " << recipient << " 的手牌 " + item << endl;
        if (hasItem(rPInfo.backpack, item) < 0) {
            string sentMsg = "SYS[#] 邪恶的 " + playerName + " 试图偷走 " + recipient + " 的 " + item + " ，然而 " + recipient + " 并没有这张牌";
            broadcastToClient(serverSock, sentMsg);
        } else {
            pInfo.backpack.push_back(item);
            eraseItem(rPInfo.backpack, item);
            clients[res].pInfo = pInfo;
            string sentMsg = "SYS[#] " + playerName + " 偷走了 " + recipient + " 的 " + item + "！";
            broadcastToClient(serverSock, sentMsg);
        }
        broadcastToClient(serverSock, "SOUNDdrawCard");
        eraseItem(pInfo.backpack, "FishingHook");
        clients[res].pInfo = pInfo;
        doSkip = true;
    }
}
void plantBomb(SOCKET serverSock, const string &playerName, const sockaddr_in &clientAddr, const int pos) {
    ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
    string res;
    PlayerInfo pInfo;
    if (cci.checkedType == FOUND_NAME) {
        pInfo = clients[cci.result].pInfo;
        res = cci.result;
    }
    if (cci.checkedType == FOUND_KEY) {
        pInfo = clients[cci.key].pInfo;
        res = cci.key;
    }

    if (cci.checkedType != NOT_FOUND) {
        if (hasItem(pInfo.backpack, "Bomb") < 0) {
            string sentMsg = "SYS[#] 你没有足够的 Bomb 埋放炸弹";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        if (pos < 0 || pos > 7) {
            string sentMsg = "SYS[#] 这个位置不合理吧...";
            sendMessage(serverSock, sentMsg, clientAddr);
            return;
        }
        broadcastToClient(serverSock, "SOUNDbitSound");
        soil[pos].doubled = true;
        soil[pos].owner = playerName;
        soil[pos].damage ++;
        cout << "[#] " << cci.result << " 翻倍了土块 " << (char) (pos + '0') << " 里的炸弹" << endl;
        string sentMsg = "SYS[#] 你向土块 " + to_string(pos) + " 里埋入了一枚炸弹";
        sendMessage(serverSock, sentMsg, clientAddr);
        sentMsg = "SYS[@] " + cci.result + " 翻倍了土块 " + (char) (pos + '0') + " 里的炸弹！";
        broadcastToClient(serverSock, sentMsg, playerName);
        eraseItem(pInfo.backpack, "Bomb");
        clients[res].pInfo = pInfo;
        doSkip = true;
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
void handleMessage(SOCKET serverSock, const string &message, sockaddr_in &clientAddr);
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

void handlePlayer(SOCKET serverSock);
bool checkTurn(SOCKET serverSock, const string &playerName, sockaddr_in &clientAddr) {
    if (plrInd >= 0 && shuffledP[plrInd].second != playerName) {
        sendMessage(serverSock, "SYS[#] 还没有轮到你呢", clientAddr);
        return false;
    }
    return true;
}
void handleMessage(SOCKET serverSock, const string &message, sockaddr_in &clientAddr) {
    lock_guard glock(gameMutex);
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
        if (inGame) {
            for (int i = 0; i < shuffledP.size(); i ++) {
                if (shuffledP[i].second == playerName) {
                    if (playerName == shuffledP[plrInd].second) doSkip = true;
                    shuffledP.erase(shuffledP.begin() + i);
                    break;
                }
            }
            plrN --;
        }
    }
    if (command(message, "CHAT")) {
        broadcastToClient(serverSock, "SOUNDboop");
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
                return;
            }
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "FLIP")) {
        if (inGame) {
            string playerName = message.substr(4);
            if (checkTurn(serverSock, playerName, clientAddr)) flipSoil(serverSock, playerName, clientAddr);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "DRAW")) {
        if (inGame) {
            string playerName = message.substr(4);
            if (checkTurn(serverSock, playerName, clientAddr)) drawCard(serverSock, playerName, clientAddr);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "DOUBLE")) {
        if (inGame) {
            int pos = message[6] - '0';
            string playerName = message.substr(7);
            if (checkTurn(serverSock, playerName, clientAddr)) doubleBomb(serverSock, playerName, clientAddr, pos);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "DETECT")) {
        if (inGame) {
            string playerName = message.substr(6);
            if (checkTurn(serverSock, playerName, clientAddr)) metalDetect(serverSock, playerName, clientAddr);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "MEDKIT")) {
        if (inGame) {
            string playerName = message.substr(6);
            if (checkTurn(serverSock, playerName, clientAddr)) {
                ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
                PlayerInfo pInfo; string res;
                if (cci.checkedType == FOUND_NAME) {
                    pInfo = clients[cci.result].pInfo;
                    res = cci.result;
                }
                if (cci.checkedType == FOUND_KEY) {
                    pInfo = clients[cci.key].pInfo;
                    res = cci.key;
                }

                if (cci.checkedType != NOT_FOUND) {
                    if (hasItem(pInfo.backpack, "MedKit") < 0) {
                        string sentMsg = "SYS[#] 你没有足够的 MedKit 恢复血量";
                        sendMessage(serverSock, sentMsg, clientAddr);
                        return;
                    }
                    pInfo.health += 2;
                    string sentMsg = "SYS[#] " + cci.result + " 使用 MedKit 恢复了 2 点体力！";

                    cout << "[#] " << cci.result << " 使用了急救箱" << endl;
                    broadcastToClient(serverSock, sentMsg);
                    eraseItem(pInfo.backpack, "MedKit");
                    clients[res].pInfo = pInfo;
                    broadcastToClient(serverSock, "SOUNDdrawCard");
                    doSkip = true;
                }
            }
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "PAN")) {
        if (inGame) {
            string playerName = message.substr(3);
            if (checkTurn(serverSock, playerName, clientAddr)) {
                ClientCheckedInfo cci = searchForClient(clientAddr, playerName);
                PlayerInfo pInfo; string res;
                if (cci.checkedType == FOUND_NAME) {
                    pInfo = clients[cci.result].pInfo;
                    res = cci.result;
                }
                if (cci.checkedType == FOUND_KEY) {
                    pInfo = clients[cci.key].pInfo;
                    res = cci.key;
                }

                if (cci.checkedType != NOT_FOUND) {
                    if (hasItem(pInfo.backpack, "FryingPan") < 0) {
                        string sentMsg = "SYS[#] 你没有足够的 FryingPan 提升抗性";
                        sendMessage(serverSock, sentMsg, clientAddr);
                        return;
                    }
                    pInfo.health += 2;
                    string sentMsg = "SYS[#] " + cci.result + " 使用 FryingPan 获得了 2 点伤害减免！";
                    cout << "[#] " << cci.result << " 使用了平底锅" << endl;
                    broadcastToClient(serverSock, sentMsg);
                    eraseItem(pInfo.backpack, "FryingPan");
                    clients[res].pInfo = pInfo;
                    broadcastToClient(serverSock, "SOUNDdrawCard");
                    doSkip = true;
                }
            }
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "MAGNIFIER")) {
        if (inGame) {
            int rnLen = stoi(message.substr(9, 2));
            string rn = message.substr(11, rnLen);
            string playerName = message.substr(rnLen + 11);
            if (checkTurn(serverSock, playerName, clientAddr)) suspect(serverSock, playerName, clientAddr, rn);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "HOOK")) {
        if (inGame) {
            int rnLen = stoi(message.substr(4, 2));
            string rn = message.substr(6, rnLen);
            int cdLen = stoi(message.substr(rnLen + 6, 2));
            string cd = message.substr(rnLen + 8, cdLen);
            string playerName = message.substr(rnLen + cdLen + 8);
            if (checkTurn(serverSock, playerName, clientAddr)) getchItem(serverSock, playerName, clientAddr, rn, cd);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (command(message, "BOMB")) {
        if (inGame) {
            int pos = message[4] - '0';
            string playerName = message.substr(5);
            if (checkTurn(serverSock, playerName, clientAddr)) plantBomb(serverSock, playerName, clientAddr, pos);
        } else sendMessage(serverSock, "SYS[#] 诶游戏还没开始呢！", clientAddr);
    }
    if (inGame) handlePlayer(serverSock);
}
void handlePlayer(SOCKET serverSock) {
    if (plrInd >= 0 && clients[shuffledP[plrInd].first].pInfo.health <= 0) {
        string deathMsg;
        switch (causeOfD) {
            case BOMB:
                deathMsg = " 跟土块爆了，就此倒地不起"; break;
            default:
                deathMsg = " 倒地不起了"; break;
        }
        broadcastToClient(serverSock, "SYS[#] " + shuffledP[plrInd].second + deathMsg);
    }

    int remainP = 0;
    string winner, winnerKey;
    for (auto &p : clients) {
        PlayerInfo pInfo = p.second.pInfo;
        if (pInfo.health > 0) {
            remainP ++;
            winner = p.second.name;
            winnerKey = p.first;
        } else {
            pInfo.score = turns * 2 - pInfo.sufferedDamage + pInfo.causedDamage * 3;
            clients[p.first].pInfo = pInfo;
        }
    }
    if (remainP == 1 || plrN == 0) {
        finished = true;

        PlayerInfo pInfo = clients[winnerKey].pInfo;
        pInfo.score = turns * 2 - pInfo.sufferedDamage + pInfo.causedDamage * 3 + 15;
        clients[winnerKey].pInfo = pInfo;
        string sentMsg = "[#] 我嘞个豆！ " + winner + " 在狂轰滥炸之下幸存了下来！\n" + PRINTLINE + "\n";
        vector<pair<string, ClientInfo>> playerList;
        for (auto &p : clients) {
            playerList.push_back({p.second.name, p.second});
        }
        sort(playerList.begin(), playerList.end(), compareP);
        for (int i = 0; i < playerList.size(); i ++) {
            sentMsg += "> [#] " + to_string(i + 1);
            if (i == 0) sentMsg += "st ";
            else if (i == 1) sentMsg += "nd ";
            else if (i == 2) sentMsg += "rd ";
            else sentMsg += "th ";
            sentMsg += playerList[i].first + " : 最终得分 " + to_string(playerList[i].second.pInfo.score);
            sentMsg += " 共造成 " + to_string(playerList[i].second.pInfo.causedDamage) + " 点伤害，共收到 " + to_string(playerList[i].second.pInfo.sufferedDamage) + " 点伤害\n";
        }
        sentMsg += PRINTLINE + "\n游戏结束，请自己退出.png";
        cout << sentMsg << endl;
        broadcastToClient(serverSock, "SYS" + sentMsg);
        inGame = false;
    } else if (doSkip) {
        if (plrN <= 0) return;
        plrInd = (plrInd + 1) % plrN;
        while (plrInd >= 0 && clients[shuffledP[plrInd].first].pInfo.health <= 0) {
            plrInd = (plrInd + 1) % plrN;
        }
        turns ++;
        broadcastToClient(serverSock, "SYS[#] 轮到 " + shuffledP[plrInd].second + " 出牌");
        doSkip = false;
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
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
                lock_guard glock(gameMutex);
                doSkip = true;
                plrInd = -1;
                shuffledP.clear();

                cout << "[@] 游戏开始！" << endl;
                initSoil();

                random_device rd;
                mt19937 gen(rd());
                uniform_int_distribution<> disCard(0, 100);
                int cardNum = max(2, (int) (clients.size() / 2 + 1));
                string sentMsg;
                for (auto p : clients) {
                    shuffledP.push_back({p.first, p.second.name});
                    PlayerInfo pInfo = p.second.pInfo;
                    for (int _ = 0; _ < cardNum; _ ++) {
                        string cRes = "Shovel";
                        int pRand = disCard(gen);
                        for (auto &g : CARD_POOL) {
                            if (pRand < g.second.maxn && pRand >= g.second.minn) {
                                cRes = g.first;
                                break;
                            }
                        }

                        pInfo.backpack.push_back(cRes);
                    }
                    clients[p.first].pInfo = pInfo;

                    sentMsg += p.second.name + " : 还有 " + to_string(p.second.pInfo.health) + " 点体力 \n";
                    sentMsg += "> [@] Ta 还有手牌 ";
                    string consoleMsg = "[@] " + p.second.name + " 还有手牌 ";
                    for (auto &s : clients[p.first].pInfo.backpack) {
                        sentMsg += s + " ";
                        consoleMsg += s + " ";
                    }
                    sentMsg += '\n';
                    cout << consoleMsg << endl;
                }
                sentMsg += PRINTLINE;
                Sleep(160);
                broadcastToClient(serverSock, "SYS[#] 游戏开始！\n" + PRINTLINE + "\n" + sentMsg);
                inGame = true;

                plrN = clients.size();
                Sleep(80);
                handlePlayer(serverSock);
            }
        }
    }
    Sleep(150);
    closesocket(serverSock);
    WSACleanup();
    return 0;
}