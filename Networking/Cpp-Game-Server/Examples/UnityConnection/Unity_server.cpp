#include <iostream>
#include <random>
#include <unordered_map>

#include "Unity_common.h"
#include "net_server.h"
#include "net_thread_safe_queue.h"

class UnityGameServer : public RCNet::net::server_interface<UnityGameMsg>
{
public:
    UnityGameServer(uint16_t nPort) : RCNet::net::server_interface<UnityGameMsg>(nPort)
    {
    }

    std::unordered_map<uint32_t, sPlayerDescription> m_mapPlayerRoster;
    std::vector<uint32_t> m_vGarbageIDs;

    // Link-up Game
    uint32_t m_LinkupSeed = 0;

    std::unordered_map<uint32_t, uint32_t> m_mapLinkupMatches; // key: player1ID, value: player2ID

protected:
    bool OnClientConnect(std::shared_ptr<RCNet::net::connection<UnityGameMsg>> client) override
    {
        // For now we will allow all
        return true;
    }

    void OnClientValidated(std::shared_ptr<RCNet::net::connection<UnityGameMsg>> client) override
    {
        // Client passed validation check, so send them a message informing
        // them they can continue to communicate
        RCNet::net::message<UnityGameMsg> msg;
        msg.header.id = UnityGameMsg::Client_Accepted;
        client->Send(msg);
    }

    void OnClientDisconnect(std::shared_ptr<RCNet::net::connection<UnityGameMsg>> client) override
    {
        if (client)
        {
            if (m_mapPlayerRoster.find(client->GetID()) == m_mapPlayerRoster.end())
            {
                // client never added to roster, so just let it disappear
            } else
            {
                auto &pd = m_mapPlayerRoster[client->GetID()];
                std::cout << "[UNGRACEFUL REMOVAL]:" + std::to_string(pd.nUniqueID) + "\n";
                m_mapPlayerRoster.erase(client->GetID());
                m_vGarbageIDs.push_back(client->GetID());
            }
        }
    }

    void OnMessage(std::shared_ptr<RCNet::net::connection<UnityGameMsg>> client,
                   RCNet::net::message<UnityGameMsg> &msg) override
    {
        if (!m_vGarbageIDs.empty())
        {
            for (auto pid: m_vGarbageIDs)
            {
                RCNet::net::message<UnityGameMsg> m;
                m.header.id = UnityGameMsg::Game_RemovePlayer;
                m << pid;
                std::cout << "Removing " << pid << "\n";
                MessageAllClients(m);
            }
            m_vGarbageIDs.clear();
        }

        switch (msg.header.id)
        {
            case UnityGameMsg::Client_RegisterWithServer:
            {
                sPlayerDescription desc;
                msg >> desc;
                desc.nUniqueID = client->GetID();
                m_mapPlayerRoster.insert_or_assign(desc.nUniqueID, desc);

                RCNet::net::message<UnityGameMsg> msgSendID;
                msgSendID.header.id = UnityGameMsg::Client_AssignID;
                msgSendID << desc.nUniqueID;
                MessageClient(client, msgSendID);

                RCNet::net::message<UnityGameMsg> msgAddPlayer;
                msgAddPlayer.header.id = UnityGameMsg::Game_AddPlayer;
                msgAddPlayer << desc;
                MessageAllClients(msgAddPlayer);

                for (const auto &player: m_mapPlayerRoster)
                {
                    // 玩家正在对战中 不显示在大厅
                    if (m_mapLinkupMatches.find(player.first) != m_mapLinkupMatches.end())
                        continue;

                    RCNet::net::message<UnityGameMsg> msgAddOtherPlayers;
                    msgAddOtherPlayers.header.id = UnityGameMsg::Game_AddPlayer;
                    msgAddOtherPlayers << player.second;
                    MessageClient(client, msgAddOtherPlayers);
                }

                break;
            }

            case UnityGameMsg::Client_UnregisterWithServer:
            {
                auto &pd = m_mapPlayerRoster[client->GetID()];
                std::cout << "[UNGRACEFUL REMOVAL]:" + std::to_string(pd.nUniqueID) + "\n";
                m_mapPlayerRoster.erase(client->GetID());
                m_vGarbageIDs.push_back(client->GetID());

                if (m_mapLinkupMatches.find(client->GetID()) != m_mapLinkupMatches.end())
                {
                    uint32_t opponentID = m_mapLinkupMatches[client->GetID()];
                    m_mapLinkupMatches.erase(client->GetID());
                    m_mapLinkupMatches.erase(opponentID);

                    RCNet::net::message<UnityGameMsg> forceExitMsg;
                    forceExitMsg.header.id = UnityGameMsg::Linkup_ForceExitGame;
                    forceExitMsg << opponentID;
                    MessageClient(opponentID, forceExitMsg);

                    std::cout << "[Linkup] Match ended due to player disconnect: "
                            << client->GetID() << " vs " << opponentID << "\n";
                }

                if (!m_vGarbageIDs.empty())
                {
                    for (auto pid: m_vGarbageIDs)
                    {
                        RCNet::net::message<UnityGameMsg> m;
                        m.header.id = UnityGameMsg::Game_RemovePlayer;
                        m << pid;
                        std::cout << "Removing " << pid << "\n";
                        MessageAllClients(m);
                    }
                    m_vGarbageIDs.clear();
                }

                break;
            }

            case UnityGameMsg::Game_UpdatePlayer:
            {
                // Simply bounce update to everyone except incoming client
                MessageAllClients(msg, client);
                break;
            }

            case UnityGameMsg::Server_GetPing:
            {
                // std::cout << "[" << client->GetID() << "]: Server Ping\n";

                // Simply bounce message back to client
                msg.header.id = UnityGameMsg::Game_Ping;
                client->Send(msg);
                break;
            }

            // Link-up Game
            case UnityGameMsg::Linkup_RequestInitSeed:
            {
                std::random_device rd; // 真随机源（来自 OS）
                std::mt19937 rng(rd()); // 高质量伪随机引擎
                std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFFFF);

                m_LinkupSeed = dist(rng); // 生成 32 位种子
                std::cout << "[Linkup] Seed generated = " << m_LinkupSeed << "\n";

                RCNet::net::message<UnityGameMsg> reply;
                reply.header.id = UnityGameMsg::Linkup_AssignInitSeed;
                reply << m_LinkupSeed;

                uint32_t player1ID, player2ID; // player1: 请求对战的玩家， player2: 被请求的玩家
                msg >> player2ID >> player1ID;
                std::cout << "[Linkup] Players " << player1ID << " and " << player2ID << " assigned seed " <<
                        m_LinkupSeed << "\n";
                m_mapLinkupMatches.insert(std::make_pair(player1ID, player2ID));
                m_mapLinkupMatches.insert(std::make_pair(player2ID, player1ID));

                reply << player1ID << player2ID;
                MessageClient(client, reply);
                MessageClient(player2ID, reply);

                // 通知其他玩家，有两名玩家开始对战，需要从大厅移除
                RCNet::net::message<UnityGameMsg> joinMsg_player1;
                RCNet::net::message<UnityGameMsg> joinMsg_player2;
                joinMsg_player1 << player1ID;
                joinMsg_player1.header.id = UnityGameMsg::Game_RemovePlayer;
                joinMsg_player2 << player2ID;
                joinMsg_player2.header.id = UnityGameMsg::Game_RemovePlayer;
                for (auto it: m_mapPlayerRoster)
                {
                    if (it.first != player1ID || it.first != player2ID)
                    {
                        MessageClient(it.first, joinMsg_player1);
                        MessageClient(it.first, joinMsg_player2);
                    }
                }

                break;
            }

            case UnityGameMsg::Linkup_SyncInitBoardStatus:
            {
                auto ReadU32LE = [&](size_t offset) -> uint32_t
                {
                    const uint8_t *p = msg.body.data() + offset;
                    return (uint32_t) p[0]
                           | ((uint32_t) p[1] << 8)
                           | ((uint32_t) p[2] << 16)
                           | ((uint32_t) p[3] << 24);
                };

                uint32_t rows = ReadU32LE(0);
                uint32_t cols = ReadU32LE(4);
                uint32_t boardPlayerID = ReadU32LE(8);
                uint32_t targetPlayerID = ReadU32LE(12);

                // 简单校验 payload 长度（每个 Cell 16 bytes）
                // 总长度 = 16 + rows*cols*16
                // 防止被恶意/错误数据搞崩
                {
                    uint64_t cellCount = (uint64_t) rows * (uint64_t) cols;
                    uint64_t needBytes = 16ull + cellCount * 16ull;
                    if (needBytes != msg.body.size())
                    {
                        std::cout << "[Linkup] SyncInitBoardStatus: size mismatch. "
                                << "rows=" << rows << " cols=" << cols
                                << " need=" << needBytes << " got=" << msg.body.size() << "\n";
                        break;
                    }
                }

                // 只允许 boardPlayerID == 发送者，避免伪造“谁发的棋盘”
                if (boardPlayerID != client->GetID())
                {
                    std::cout << "[Linkup] SyncInitBoardStatus: spoof detected. "
                            << "body.boardPlayerID=" << boardPlayerID
                            << " clientID=" << client->GetID() << "\n";
                    break;
                }

                std::cout << "[Linkup] SyncInitBoardStatus: "
                        << "rows=" << rows << " cols=" << cols
                        << " boardPlayerID=" << boardPlayerID
                        << " targetPlayerID=" << targetPlayerID << "\n";

                RCNet::net::message<UnityGameMsg> forward;
                forward.header.id = UnityGameMsg::Linkup_SyncInitBoardStatus;
                forward.body = msg.body; // 直接拷贝 body（保持与 C# 兼容的布局）
                forward.header.size = (uint32_t) forward.body.size();

                // 发送给目标玩家
                MessageClient(targetPlayerID, forward);

                break;
            }

            case UnityGameMsg::Linkup_Eliminate:
            {
                if (msg.body.size() < 12) break;

                auto ReadU32LE = [&](size_t offset) -> uint32_t
                {
                    const uint8_t *p = msg.body.data() + offset;
                    return (uint32_t) p[0]
                           | ((uint32_t) p[1] << 8)
                           | ((uint32_t) p[2] << 16)
                           | ((uint32_t) p[3] << 24);
                };

                uint32_t x = ReadU32LE(0);
                uint32_t y = ReadU32LE(4);
                uint32_t playerID = ReadU32LE(8);

                auto it = m_mapLinkupMatches.find(playerID);
                if (it == m_mapLinkupMatches.end()) break;

                uint32_t targetPlayerID = it->second;

                RCNet::net::message<UnityGameMsg> forward;
                forward.header.id = UnityGameMsg::Linkup_Eliminate;
                forward.body = msg.body;
                forward.header.size = (uint32_t) forward.body.size();

                MessageClient(targetPlayerID, forward);

                // std::cout << "[Linkup] Eliminate: (" << x << "," << y << ") from "
                //         << playerID << " -> " << targetPlayerID << "\n";

                break;
            }

            case UnityGameMsg::Linkup_PlayerWin:
            {
                uint32_t playerID;
                msg >> playerID;

                auto it = m_mapLinkupMatches.find(playerID);
                if (it == m_mapLinkupMatches.end()) break;

                uint32_t targetPlayerID = it->second;

                RCNet::net::message<UnityGameMsg> loseMsg;
                loseMsg.header.id = UnityGameMsg::Linkup_PlayerLose;
                loseMsg << targetPlayerID;
                MessageClient(targetPlayerID, loseMsg);

                std::cout << "[Linkup] PlayerWin: " << playerID << " wins over " << targetPlayerID << "\n";

                break;
            }
        }
    }
};

int main()
{
    UnityGameServer server(60000);
    server.Start();

    while (1)
    {
        server.Update(-1, true);
    }
    return 0;
}
