#ifndef GAME_NETWORK_H
#define GAME_NETWORK_H

#include <cstdint>

class Game;

// 从 Game 类中提取的网络消息处理模块
// 负责解析服务端推送的 Protobuf 消息并更新游戏状态
class GameNetwork {
public:
    explicit GameNetwork(Game& game);

    // 处理所有待处理的网络消息（每帧调用一次）
    void processMessages();

private:
    Game& m_game;
};

#endif
