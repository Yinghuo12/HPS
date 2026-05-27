#ifndef GAME_H
#define GAME_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "game_object.h"
#include "sprite_renderer.h"
#include "sprite_batch.h"
#include "resource_manager.h"
#include "terrain.h"
#include "projectile.h"
#include "text_renderer.h"
#include "camera.h"
#include "particle_system.h"

enum GameState {
    GAME_LOGIN,
    GAME_MENU,
    GAME_WAITING,
    GAME_PLAYING,
    GAME_OVER
};

enum CameraMode {
    CAM_INTRO,
    CAM_FOLLOW_TURN,
    CAM_FOLLOW_PROJ,
    CAM_MANUAL,
    CAM_FREE
};

struct RoomSlot {
    uint32_t player_id;
    std::string player_name;
    int team;       // 0=RED, 1=BLUE
    bool ready;
};

struct RoomInfoClient {
    uint32_t room_id;
    std::string room_name;
    int player_count;
    int max_players;
    bool game_started;
    std::vector<RoomSlot> players;
};

class Game {
public:
    static const GLuint WORLD_W = 3000;
    static const GLuint WORLD_H = 1400;

    GameState  State;
    GLboolean  Keys[1024];
    GLuint     Width, Height;

    Game(GLuint width, GLuint height);
    ~Game();

    void Init();
    void ProcessInput(GLfloat dt);
    void Update(GLfloat dt);
    void Render();
    void RenderImGui();
    void Shutdown();

    // Game objects
    GameObject* Players[2];
    SpriteRenderer* Renderer;
    SpriteBatch* Batch;
    Terrain* GameTerrain;
    Projectile projectile;
    ParticleEmitter* m_trailEmitter;
    TextRenderer* Text;
    Camera* m_camera;

    // Game state
    int   CurrentAngle;
    int   OpponentAngle;
    float Power;
    bool  IsCharging;
    bool  IsMyTurn;
    float Wind;
    std::string MyName;
    uint32_t MyId;
    int   HP[2];
    uint32_t TurnNumber;
    int   Directions[2];
    int   MyIndex;
    int   m_lastShooterIdx;

    // Status info
    std::string StatusText;
    std::string ServerAddr;

    // Room system
    uint32_t CurrentRoomId;
    std::vector<RoomInfoClient> RoomList;
    bool m_myReady;

    // Mouse
    double m_mouseX, m_mouseY;
    bool   m_mousePressed;
    bool   m_rightMousePressed;
    bool   m_minimapDragging;
    bool   m_cameraDragging;
    double m_dragLastX, m_dragLastY;
    bool   m_quitRequested;

    // Camera mode
    CameraMode m_cameraMode;
    float m_manualTimer;
    int   m_introPhase;
    float m_introTimer;
    CameraMode m_prevModeBeforeManual;

private:
    void processNetworkMessages();
    void RenderMenuPanel();
    void RenderWaitingPanel();
    void RenderGameHUD();
    void RenderGameOverPanel();
    void RenderChatPanel();
    void RenderRoomListPanel();
    void RenderRoomLobbyPanel();
    void renderMinimap();
    void updateCamera(float dt);
    float m_lastMoveTime;
    bool m_imguiCharging;
    bool m_loggedIn;
    std::string m_password;

    // Chat
    struct ChatMsg {
        int channel;
        uint32_t sender_id;
        std::string sender_name;
        std::string message;
        uint64_t timestamp;
    };
    std::vector<ChatMsg> m_chatMessages;
    int m_currentChannel;
    char m_chatInput[256];
    bool m_chatScrollToBottom;

    // Helpers
    GameObject* myPlayer();
    GameObject* opponentPlayer();
    int myHP();
    int opponentHP();
    std::string formatInt(int val);

    // Explosion effects
    struct ExplosionFX {
        glm::vec2 pos;
        float timer;
        int frame;   // 0-3
        bool active;
    };
    std::vector<ExplosionFX> m_explosions;

    // Floating damage numbers
    struct DamageFloat {
        glm::vec2 pos;
        std::string text;
        float timer;
        glm::vec3 color;
        bool active;
    };
    std::vector<DamageFloat> m_damageFloats;
};

#endif
