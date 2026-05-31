#include "game_hud.h"
#include "game.h"
#include "network_client.h"
#include "ddt.pb.h"
#include "common/ddt_physics.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <cstring>

#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_RESET   "\033[0m"

char GameHUD::s_nameBuf[64] = "player";
char GameHUD::s_passBuf[64] = "";
char GameHUD::s_serverBuf[128] = "127.0.0.1:8073";

GameHUD::GameHUD(Game& game) : m_game(game) {}

void GameHUD::RenderAll() {
    RenderQuitDialog();

    switch (m_game.State) {
        case 0: // GAME_LOGIN
        case 1: // GAME_MENU
            RenderMenuPanel(); break;
        case 2: // GAME_WAITING
            RenderWaitingPanel(); break;
        case 3: // GAME_PLAYING
            RenderGameHUD(); break;
        case 4: // GAME_OVER
            RenderGameHUD();
            RenderGameOverPanel();
            break;
    }
    if (m_game.State != 0) RenderChatPanel();
}

void GameHUD::RenderQuitDialog() {
    if (m_game.m_quitRequested) {
        ImGui::OpenPopup("Quit Game?");
        m_game.m_quitRequested = false;
    }
    if (ImGui::BeginPopupModal("Quit Game?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Are you sure you want to quit?");
        if (m_game.State == 3) { // GAME_PLAYING
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                "WARNING: You will lose the current game!");
        }
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(120, 0))) {
            m_game.Shutdown();
            glfwSetWindowShouldClose(glfwGetCurrentContext(), GL_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GameHUD::RenderMenuPanel() {
    if (m_game.State == 0) { // GAME_LOGIN
        ImGui::SetNextWindowPos(
            ImVec2(m_game.Width / 2.0f - 180.0f, m_game.Height / 2.0f - 140.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);

        ImGui::Begin("DDT - Login", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("DDT - %s", u8"\xe5\xbc\xb9\xe5\xbc\xb9\xe5\xa0\x82");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();
    
        ImGui::Text("Server Address:");
        ImGui::InputText("##server", s_serverBuf, sizeof(s_serverBuf));
        ImGui::Spacing();
        ImGui::Text("Player Name:");
        ImGui::InputText("##name", s_nameBuf, sizeof(s_nameBuf));
        ImGui::Text("Password:");
        ImGui::InputText("##pass", s_passBuf, sizeof(s_passBuf), ImGuiInputTextFlags_Password);
        ImGui::Spacing();
    
        if (ImGui::Button("Login", ImVec2(100, 30))) {
            m_game.ServerAddr = std::string(s_serverBuf);
            m_game.MyName = std::string(s_nameBuf);
            m_game.m_password = std::string(s_passBuf);
            ddt::NetworkClient::Instance().disconnect();
            m_game.StatusText = "CONNECTING...";
            auto url = "ws://" + m_game.ServerAddr + "/ddt/game";
            bool ok = ddt::NetworkClient::Instance().connect(url);
            if (!ok) {
                m_game.StatusText = "SERVER UNREACHABLE - Check address or try later";
                std::cout << C_RED << "> Connect to " << m_game.ServerAddr << " FAILED" << C_RESET << std::endl;
            } else {
                ddt::NetworkClient::Instance().enableAutoReconnect(url);
                ddt::NetworkClient::Instance().sendLogin(m_game.MyName, m_game.m_password);
                m_game.StatusText = "LOGGING IN...";
                std::cout << C_YELLOW << "> Connect to " << m_game.ServerAddr
                          << " as \"" << m_game.MyName << "\"" << C_RESET << std::endl;
            }
        }
    
        ImGui::SameLine();
    
        if (ImGui::Button("Register", ImVec2(100, 30))) {
            m_game.ServerAddr = std::string(s_serverBuf);
            m_game.MyName = std::string(s_nameBuf);
            m_game.m_password = std::string(s_passBuf);
            ddt::NetworkClient::Instance().disconnect();
            m_game.StatusText = "CONNECTING...";
            auto url = "ws://" + m_game.ServerAddr + "/ddt/game";
            bool ok = ddt::NetworkClient::Instance().connect(url);
            if (!ok) {
                m_game.StatusText = "SERVER UNREACHABLE - Check address or try later";
            } else {
                ddt::NetworkClient::Instance().enableAutoReconnect(url);
                ddt::NetworkClient::Instance().sendRegister(m_game.MyName, m_game.m_password);
                m_game.StatusText = "REGISTERING...";
                std::cout << C_YELLOW << "> Register as \"" << m_game.MyName << "\"" << C_RESET << std::endl;
            }
        }
    
        ImGui::Spacing();
        ImGui::TextDisabled("%s", m_game.StatusText.c_str());
        ImGui::End();
    } else {
        RenderRoomListPanel();
    }
}

void GameHUD::RenderWaitingPanel() {
    RenderRoomLobbyPanel();
}

void GameHUD::RenderGameHUD() {
    ImGuiWindowFlags hudFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(20, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250, 55), ImGuiCond_Always);
    ImGui::Begin("##hp1", nullptr, hudFlags);
    ImGui::Text("P1 %s", (m_game.MyIndex == 0 ? "(You)" : ""));
    ImGui::SameLine();
    char hp1Label[16];
    snprintf(hp1Label, sizeof(hp1Label), "%d/100", m_game.HP[0]);
    ImGui::ProgressBar(m_game.HP[0] / 100.0f, ImVec2(160, 16), hp1Label);
    ImGui::End();
    
    ImGui::SetNextWindowPos(ImVec2(m_game.Width - 280, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250, 55), ImGuiCond_Always);
    ImGui::Begin("##hp2", nullptr, hudFlags);
    ImGui::Text("P2 %s", (m_game.MyIndex == 1 ? "(You)" : ""));
    ImGui::SameLine();
    char hp2Label[16];
    snprintf(hp2Label, sizeof(hp2Label), "%d/100", m_game.HP[1]);
    ImGui::ProgressBar(m_game.HP[1] / 100.0f, ImVec2(160, 16), hp2Label);
    ImGui::End();
    
    ImGui::SetNextWindowPos(ImVec2(m_game.Width / 2.0f - 100, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, 70), ImGuiCond_Always);
    ImGui::Begin("##info", nullptr, hudFlags);
    if (m_game.IsMyTurn) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ">> YOUR TURN <<");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "OPPONENT TURN");
    }
    ImGui::Text("Wind: %d  |  Turn: %d", (int)m_game.Wind, (int)m_game.TurnNumber);
    ImGui::End();
    
    if (m_game.IsMyTurn && m_game.State == 3) { // GAME_PLAYING
        ImGui::SetNextWindowPos(ImVec2(10, m_game.Height - 80), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_game.Width - 20, 70), ImGuiCond_Always);
        ImGui::Begin("##controls", nullptr, hudFlags);
    
        ImGui::PushItemWidth(180);
        ImGui::SliderInt("Angle", &m_game.CurrentAngle, m_game.m_effectiveAngleMin, m_game.m_effectiveAngleMax);
        ImGui::PopItemWidth();
    
        ImGui::SameLine();
        
        // 【修改】：玩家开火后，彻底禁用所有的按钮操作
        if (m_game.m_hasShot) {
            ImGui::BeginDisabled();
        }
    
        if (ImGui::Button("< Move")) {
            auto* me = m_game.myPlayer();
            if (me->Position.x > 0) {
                float dx = -20.0f;
                me->Position.x += dx;
                if (me->Position.x < 0) me->Position.x = 0;
                m_game.Directions[m_game.MyIndex] = 0;
                ddt::NetworkClient::Instance().sendMove(dx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Move >")) {
            auto* me = m_game.myPlayer();
            if (me->Position.x < m_game.WORLD_W - me->Size.x) {
                float dx = 20.0f;
                me->Position.x += dx;
                if (me->Position.x > m_game.WORLD_W - me->Size.x)
                    me->Position.x = m_game.WORLD_W - me->Size.x;
                m_game.Directions[m_game.MyIndex] = 1;
                ddt::NetworkClient::Instance().sendMove(dx);
            }
        }
    
        ImGui::SameLine();
    
        if (ImGui::Button("PASS (P)", ImVec2(80, 30))) {
            m_game.m_hasShot = true;
            ddt::NetworkClient::Instance().sendPass();
        }
    
        ImGui::SameLine();
        if (m_game.m_flyCooldown > 0) {
            ImGui::TextDisabled("Airplane (%d)", m_game.m_flyCooldown);
        } else {
            if (m_game.m_useFlyItem) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            }
            if (ImGui::Button("Paper Plane")) {
                m_game.m_useFlyItem = !m_game.m_useFlyItem;
            }
            if (m_game.m_useFlyItem) ImGui::PopStyleColor();
        }
        
        if (m_game.m_hasShot) {
            ImGui::EndDisabled();
        }
    
        ImGui::SameLine();
        ImGui::Text("Power: %d%%", (int)m_game.Power);
        ImGui::SameLine();
        ImGui::ProgressBar(m_game.Power / 100.0f, ImVec2(120, 16), "");
    
        ImGui::End();
    }
}

void GameHUD::RenderGameOverPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(m_game.Width / 2.0f - 160.0f, m_game.Height / 2.0f - 90.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);

    ImGui::Begin("Game Over", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    bool won = (m_game.StatusText == "YOU WIN!");
    if (won) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "YOU WIN!");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "YOU LOSE!");
    }
    
    ImGui::Separator();
    ImGui::Text("Final HP - P1: %d  P2: %d", m_game.HP[0], m_game.HP[1]);
    ImGui::Spacing();
    
    if (ImGui::Button("Back to Room", ImVec2(200, 30))) {
        m_game.State = GAME_WAITING;
        m_game.StatusText = ""; // 离开结算必须清空文案触发位
        m_game.HP[0] = 100; m_game.HP[1] = 100;
        m_game.IsMyTurn = false;
        m_game.IsCharging = false;
        m_game.Power = 0.0f;
        m_game.CurrentAngle = 60;
        m_game.TurnNumber = 0;
        m_game.m_myReady = false;
        m_game.m_hasShot = false;
        m_game.m_moveUsed = 0.0f;
        // [BugFix] 清除上一局残留的视觉元素
        m_game.m_explosions.clear();
        m_game.m_damageFloats.clear();
        m_game.m_pendingHit.active = false;
        m_game.projectile.Reset();
        // 保留 CurrentRoomId 和 MyIndex，仍在房间内
        ddt::NetworkClient::Instance().sendRoomList();
    }
    
    ImGui::End();
}

void GameHUD::RenderChatPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(m_game.Width - 370.0f, m_game.Height - 320.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 310), ImGuiCond_FirstUseEver);

    ImGui::Begin("Chat");
    
    const char* channelNames[] = {"World", "Room", "Team", "All", "Private"};
    int channelIds[] = {3, 2, 0, 1, 6};
    for (int i = 0; i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (m_game.m_currentChannel == channelIds[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::SmallButton(channelNames[i])) {
            m_game.m_currentChannel = channelIds[i];
        }
        if (active) ImGui::PopStyleColor();
    }
    
    ImGui::Separator();
    
    ImGui::BeginChild("##chat_messages", ImVec2(0, -40), true);
    for (auto& cm : m_game.m_chatMessages) {
        if (cm.channel != m_game.m_currentChannel) continue;
        ImVec4 color;
        switch (cm.channel) {
            case 4: color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); break;
            case 6: color = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
            case 0: color = ImVec4(0.0f, 1.0f, 0.5f, 1.0f); break;
            default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
        }
        std::string line = "[" + cm.sender_name + "] " + cm.message;
        ImGui::TextColored(color, "%s", line.c_str());
    }
    if (m_game.m_chatScrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_game.m_chatScrollToBottom = false;
    }
    ImGui::EndChild();
    
    ImGui::Spacing();
    bool enterPressed = ImGui::InputText("##chat_input", m_game.m_chatInput, sizeof(m_game.m_chatInput),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (enterPressed || ImGui::Button("Send")) {
        if (m_game.m_chatInput[0] != '\0') {
            ddt::NetworkClient::Instance().sendChat(m_game.m_currentChannel, std::string(m_game.m_chatInput));
            memset(m_game.m_chatInput, 0, sizeof(m_game.m_chatInput));
        }
    }
    
    ImGui::End();
}

void GameHUD::RenderRoomListPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(m_game.Width / 2.0f - 250.0f, 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, m_game.Height - 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("DDT - Room List");
    
    ImGui::Text("Welcome, %s!", m_game.MyName.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Logout")) {
        m_game.State = GAME_LOGIN;
        m_game.m_loggedIn = false;
        ddt::NetworkClient::Instance().disconnect();
    }
    
    ImGui::Separator();
    
    static char roomNameBuf[64] = "";
    ImGui::InputText("Room Name", roomNameBuf, sizeof(roomNameBuf));
    ImGui::SameLine();
    if (ImGui::Button("Create Room")) {
        std::string name = roomNameBuf[0] ? std::string(roomNameBuf) : (m_game.MyName + "'s Room");
        ddt::NetworkClient::Instance().sendCreateRoom(name);
        std::cout << C_YELLOW << "> Create room: " << name << C_RESET << std::endl;
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        ddt::NetworkClient::Instance().sendRoomList();
    }
    
    ImGui::Separator();
    
    if (m_game.RoomList.empty()) {
        ImGui::TextDisabled("No rooms available. Create one!");
    } else {
        ImGui::BeginChild("##rooms", ImVec2(0, 0), true);
        for (auto& room : m_game.RoomList) {
            ImGui::PushID(room.room_id);
    
            bool isFull = room.player_count >= room.max_players;
            bool inProgress = room.game_started;
    
            std::string header = "Room #" + std::to_string(room.room_id) +
                " - " + room.room_name +
                " [" + std::to_string(room.player_count) + "/" +
                std::to_string(room.max_players) + "]";
    
            if (inProgress) header += " [PLAYING]";
            else if (isFull) header += " [FULL]";
    
            if (ImGui::CollapsingHeader(header.c_str())) {
                ImGui::Indent();
    
                for (auto& slot : room.players) {
                    ImVec4 nameColor = (slot.team == 0)
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                        : ImVec4(0.3f, 0.5f, 1.0f, 1.0f);
                    std::string teamTag = (slot.team == 0) ? "[RED] " : "[BLUE] ";
                    std::string readyTag = slot.ready ? " [READY]" : "";
                    ImGui::TextColored(nameColor, "%s%s%s",
                        teamTag.c_str(), slot.player_name.c_str(), readyTag.c_str());
                }
    
                if (!isFull && !inProgress) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Join RED")) {
                        ddt::NetworkClient::Instance().sendJoinRoom(room.room_id, 0);
                        m_game.CurrentRoomId = room.room_id;
                        m_game.m_myReady = false;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.3f, 0.7f, 1.0f));
                    if (ImGui::Button("Join BLUE")) {
                        ddt::NetworkClient::Instance().sendJoinRoom(room.room_id, 1);
                        m_game.CurrentRoomId = room.room_id;
                        m_game.m_myReady = false;
                    }
                    ImGui::PopStyleColor();
                } else if (isFull) {
                    ImGui::TextDisabled("Room is full");
                } else {
                    ImGui::TextDisabled("Game in progress");
                }
    
                ImGui::Unindent();
            }
    
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    
    ImGui::End();
}

void GameHUD::RenderRoomLobbyPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(m_game.Width / 2.0f - 250.0f, 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, m_game.Height - 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("DDT - Room Lobby");
    
    ImGui::Text("Room #%d", m_game.CurrentRoomId);
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::Button("Leave Room")) {
        ddt::NetworkClient::Instance().sendLeaveRoom();
        m_game.CurrentRoomId = 0;
        m_game.m_myReady = false;
        m_game.State = GAME_MENU;
        ddt::NetworkClient::Instance().sendRoomList();
    }
    
    ImGui::Separator();
    
    RoomInfoClient* currentRoom = nullptr;
    for (size_t i = 0; i < m_game.RoomList.size(); i++) {
        if (m_game.RoomList[i].room_id == m_game.CurrentRoomId) {
            currentRoom = &m_game.RoomList[i];
            break;
        }
    }
    
    if (!currentRoom) {
        float t = ImGui::GetTime();
        int phase = (int)(t * 2.0f) % 4;
        const char* dots[] = {"", ".", "..", "..."};
        ImGui::Text("Loading room info%s", dots[phase]);
        ImGui::End();
        return;
    }
    
    ImGui::Text("%s  [%d/%d players]",
        currentRoom->room_name.c_str(),
        currentRoom->player_count,
        currentRoom->max_players);
    
    ImGui::Spacing();
    
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "--- RED TEAM ---");
    for (size_t i = 0; i < currentRoom->players.size(); i++) {
        auto& slot = currentRoom->players[i];
        if (slot.team != 0) continue;
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "  %s", slot.player_name.c_str());
        if (slot.ready) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "READY");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("not ready");
        }
        if (slot.player_id == m_game.MyId) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Switch BLUE##me")) {
                m_game.m_myReady = false;
                ddt::NetworkClient::Instance().sendSwitchTeam(1);
            }
        }
    }
    ImGui::EndGroup();
    
    ImGui::SameLine();
    
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "--- BLUE TEAM ---");
    for (size_t i = 0; i < currentRoom->players.size(); i++) {
        auto& slot = currentRoom->players[i];
        if (slot.team != 1) continue;
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "  %s", slot.player_name.c_str());
        if (slot.ready) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "READY");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("not ready");
        }
        if (slot.player_id == m_game.MyId) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Switch RED##me")) {
                m_game.m_myReady = false;
                ddt::NetworkClient::Instance().sendSwitchTeam(0);
            }
        }
    }
    ImGui::EndGroup();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (m_game.m_myReady) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
        if (ImGui::Button("Cancel Ready", ImVec2(200, 40))) {
            m_game.m_myReady = false;
            ddt::NetworkClient::Instance().sendReady(false);
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
        if (ImGui::Button("READY", ImVec2(200, 40))) {
            m_game.m_myReady = true;
            ddt::NetworkClient::Instance().sendReady(true);
        }
        ImGui::PopStyleColor(2);
    }
    
    ImGui::Spacing();
    ImGui::TextDisabled("All players must be ready. Both teams need at least 1 player.");
    
    ImGui::End();
}