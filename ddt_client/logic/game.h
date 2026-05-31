#ifndef GAME_H
#define GAME_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include "game_object.h"
#include "sprite_renderer.h"
#include "sprite_batch.h"
#include "resource_manager.h"
#include "terrain.h"
#include "projectile.h"
#include "text_renderer.h"
#include "camera.h"
#include "particle_system.h"
#include "common/ddt_physics.h"

class GameHUD;          // forward declaration
class GameNetwork;      // forward declaration
class GameRenderer;     // forward declaration

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
    int team;       
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

    friend class GameHUD;
    friend class GameNetwork;
    friend class GameRenderer;

    std::unique_ptr<GameObject> Players[2];
    std::unique_ptr<SpriteRenderer> Renderer;
    std::unique_ptr<SpriteBatch> Batch;
    std::unique_ptr<Terrain> GameTerrain;
    Projectile projectile;
    std::unique_ptr<ParticleEmitter> m_trailEmitter;
    std::unique_ptr<TextRenderer> Text;
    std::unique_ptr<Camera> m_camera;

    int   CurrentAngle;
    int   OpponentAngle;
    float Power;
    bool  IsCharging;
    bool  m_hasShot;
    bool  IsMyTurn;
    float Wind;
    std::string MyName;
    uint32_t MyId;
    int   HP[2];
    uint32_t TurnNumber;
    int   Directions[2];
    int   MyIndex;
    int   m_lastShooterIdx;

    std::string StatusText;
    std::string ServerAddr;

    uint32_t CurrentRoomId;
    std::vector<RoomInfoClient> RoomList;
    bool m_myReady;

    double m_mouseX, m_mouseY;
    bool   m_mousePressed;
    bool   m_rightMousePressed;
    bool   m_minimapDragging;
    bool   m_cameraDragging;
    double m_dragLastX, m_dragLastY;
    bool   m_quitRequested;

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
    bool m_loggedIn;
    std::string m_password;

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

    GameObject* myPlayer();
    GameObject* opponentPlayer();
    int myHP();
    int opponentHP();
    std::string formatInt(int val);

    struct ExplosionFX {
        glm::vec2 pos;
        float timer;
        int frame;   
        bool active;
    };
    std::vector<ExplosionFX> m_explosions;

    struct DamageFloat {
        glm::vec2 pos;
        std::string text;
        float timer;
        glm::vec3 color;
        bool active;
    };
    std::vector<DamageFloat> m_damageFloats;

    // 添加延迟爆炸缓存、单回合已经移动的距离限额
    struct PendingHit {
        bool active = false;
        float hit_x, hit_y;
        bool hit_player;
        uint32_t hit_player_id;
        int damage;
        int damage_type;
        int hp0, hp1;
        glm::vec2 pos0, pos1;
    } m_pendingHit;

    float m_moveUsed; // 记录本回合玩家已行走的距离 (max: 200.0f)

    int m_effectiveAngleMin = 20;
    int m_effectiveAngleMax = 65;

    // 服务端下发的物理参数，用于客户端本地弹道计算
    ddt::PhysicsParams m_serverPhysics;
    float m_maxMovePerTurn = 200.0f;  // 服务端下发的移动限额，替代硬编码

    bool m_useFlyItem = false;  // 是否使用飞行道具
    int  m_flyCooldown = 0;     // 飞行道具冷却时间，单位为回合数
    bool m_projIsFly = false;   // 当前发射的弹道是否为飞行弹道
};

#endif