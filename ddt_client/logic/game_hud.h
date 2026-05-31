#ifndef GAME_HUD_H
#define GAME_HUD_H

#include <string>
#include <vector>
#include <cstdint>

class Game;

// 从 Game 类中提取的 ImGui HUD/面板渲染模块
class GameHUD {
public:
    explicit GameHUD(Game& game);

    void RenderAll();            // 顶层：渲染所有 ImGui 面板
    void RenderQuitDialog();
    void RenderMenuPanel();
    void RenderWaitingPanel();
    void RenderGameHUD();
    void RenderGameOverPanel();
    void RenderChatPanel();
    void RenderRoomListPanel();
    void RenderRoomLobbyPanel();

private:
    Game& m_game;

    static char s_nameBuf[64];
    static char s_passBuf[64];
    static char s_serverBuf[128];
};

#endif
