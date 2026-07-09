#include "game.h"
#include "game_network.h"
#include "game_renderer.h"
#include "game_hud.h"
#include "network_client.h"
#include "ddt.pb.h"
#include "common/ddt_physics.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>
#include <cmath>
#include <fstream>

static std::string g_assetBase;

static void findAssetBase() {
    const char* candidates[] = {
        "assets/",                    
        "src/client/assets/",         
        "../src/client/assets/",      
        "../../src/client/assets/",   
    };
    for (auto prefix : candidates) {
        std::string path = std::string(prefix) + "role.png";
        std::ifstream f(path);
        if (f.good()) {
            f.close();
            g_assetBase = prefix;
            std::cout << "[Assets] Found at: " << prefix << std::endl;
            return;
        }
    }
    g_assetBase = "assets/";
    std::cerr << "[Assets] WARNING: asset directory not found!" << std::endl;
}

#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"
#define C_RESET   "\033[0m"

static const GLchar* SPRITE_VERT_SRC =
    "#version 330 core\n"
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 model;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    TexCoords = vertex.zw;\n"
    "    gl_Position = projection * model * vec4(vertex.xy, 0.0, 1.0);\n"
    "}\n";

static const GLchar* SPRITE_FRAG_SRC =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D image;\n"
    "uniform vec3 spriteColor;\n"
    "void main() {\n"
    "    FragColor = vec4(spriteColor, 1.0) * texture(image, TexCoords);\n"
    "}\n";

static const GLchar* BATCH_VERT_SRC =
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "layout (location = 2) in vec4 aColor;\n"
    "out vec2 TexCoords;\n"
    "out vec4 VertexColor;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    TexCoords = aTexCoord;\n"
    "    VertexColor = aColor;\n"
    "    gl_Position = projection * vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const GLchar* BATCH_FRAG_SRC =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "in vec4 VertexColor;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D image;\n"
    "void main() {\n"
    "    FragColor = VertexColor * texture(image, TexCoords);\n"
    "}\n";

Game::Game(GLuint width, GLuint height)
    : State(GAME_LOGIN), Width(width), Height(height)
    , CurrentAngle(60), OpponentAngle(60)
    , Power(0.0f), IsCharging(false)
    , m_hasShot(false), IsMyTurn(false), Wind(0.0f), MyId(0)
    , TurnNumber(0), MyIndex(-1)
    , m_lastShooterIdx(0)
    , ServerAddr("127.0.0.1:8073")
    , CurrentRoomId(0)
    , m_myReady(false)
    , m_mouseX(0), m_mouseY(0)
    , m_mousePressed(false), m_rightMousePressed(false)
    , m_minimapDragging(false), m_cameraDragging(false)
    , m_dragLastX(0), m_dragLastY(0), m_quitRequested(false)
    , m_cameraMode(CAM_FREE), m_manualTimer(0)
    , m_introPhase(0), m_introTimer(0)
    , m_prevModeBeforeManual(CAM_FREE)
    , m_lastMoveTime(0.0f)
    , m_loggedIn(false)
    , m_password()
    , m_currentChannel(3)
    , m_chatScrollToBottom(false) {
    memset(Keys, 0, sizeof(Keys));
    memset(m_chatInput, 0, sizeof(m_chatInput));
    HP[0] = 100; HP[1] = 100;
    Directions[0] = 1; Directions[1] = 0;
    m_pendingHit.active = false;
    m_moveUsed = 0.0f;
}

Game::~Game() {
    // ResourceManager::Clear() 已移至 Shutdown() 中在 GL context 销毁前调用
}

void Game::Init() {
    findAssetBase();

    ResourceManager::LoadShaderFromSource(SPRITE_VERT_SRC, SPRITE_FRAG_SRC, "sprite");
    
    glm::mat4 projection = glm::ortho(0.0f, static_cast<GLfloat>(Width),
                                       static_cast<GLfloat>(Height), 0.0f, -1.0f, 1.0f);
    ResourceManager::GetShader("sprite").Use();
    ResourceManager::GetShader("sprite").SetInteger("image", 0);
    ResourceManager::GetShader("sprite").SetMatrix4("projection", projection);
    
    ResourceManager::LoadTexture((g_assetBase + "role.png").c_str(), true, "player1");
    ResourceManager::LoadTexture((g_assetBase + "role_r.png").c_str(), true, "player1_r");
    ResourceManager::LoadTexture((g_assetBase + "role2.png").c_str(), true, "player2");
    ResourceManager::LoadTexture((g_assetBase + "role2_r.png").c_str(), true, "player2_r");
    
    ResourceManager::LoadTexture((g_assetBase + "tri_darts.png").c_str(), true, "proj_p1");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_r.png").c_str(), true, "proj_p1_r");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_bomb.png").c_str(), true, "proj_p1_bomb");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_bomb_r.png").c_str(), true, "proj_p1_bomb_r");
    
    ResourceManager::LoadTexture((g_assetBase + "ice_cream.png").c_str(), true, "proj_p2");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_r.png").c_str(), true, "proj_p2_r");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_bomb.png").c_str(), true, "proj_p2_bomb");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_bomb_r.png").c_str(), true, "proj_p2_bomb_r");
    
    ResourceManager::LoadTexture((g_assetBase + "bow0.png").c_str(), true, "explosion_0");
    ResourceManager::LoadTexture((g_assetBase + "bow1.png").c_str(), true, "explosion_1");
    ResourceManager::LoadTexture((g_assetBase + "bow2.png").c_str(), true, "explosion_2");
    ResourceManager::LoadTexture((g_assetBase + "bow3.png").c_str(), true, "explosion_3");
    
    ResourceManager::LoadTexture((g_assetBase + "fly.png").c_str(), true, "fly");
    ResourceManager::LoadTexture((g_assetBase + "flyAttack.png").c_str(), true, "fly_attack");
    ResourceManager::LoadTexture((g_assetBase + "flyAttack_r.png").c_str(), true, "fly_attack_r");
    ResourceManager::LoadTexture((g_assetBase + "bomb.png").c_str(), true, "bomb_icon");
    
    ResourceManager::LoadTexture((g_assetBase + "bg_rainbow.png").c_str(), false, "sky");
    ResourceManager::LoadTexture((g_assetBase + "bg_ghost.png").c_str(), false, "sky_ghost");
    
    std::cout << "[Init] Textures loaded" << std::flush;
    
    GLubyte imgBarrel[8 * 32 * 4];
    for (int i = 0; i < 8 * 32; i++) {
        imgBarrel[i*4+0] = 100; imgBarrel[i*4+1] = 100; imgBarrel[i*4+2] = 100; imgBarrel[i*4+3] = 255;
    }
    ResourceManager::GetTexture("barrel").Generate(8, 32, imgBarrel);
    
    GLubyte imgBar[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        imgBar[i*4+0] = 0; imgBar[i*4+1] = 255; imgBar[i*4+2] = 0; imgBar[i*4+3] = 200;
    }
    ResourceManager::GetTexture("powerbar").Generate(4, 4, imgBar);
    
    GLubyte imgHP[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        imgHP[i*4+0] = 255; imgHP[i*4+1] = 50; imgHP[i*4+2] = 50; imgHP[i*4+3] = 220;
    }
    ResourceManager::GetTexture("hpbar").Generate(4, 4, imgHP);
    
    GLubyte imgHPBg[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        imgHPBg[i*4+0] = 80; imgHPBg[i*4+1] = 80; imgHPBg[i*4+2] = 80; imgHPBg[i*4+3] = 200;
    }
    ResourceManager::GetTexture("hpbar_bg").Generate(4, 4, imgHPBg);
    
    std::cout << " | Renderers" << std::flush;
    Renderer = std::unique_ptr<SpriteRenderer>(new SpriteRenderer(ResourceManager::GetShader("sprite")));
    
    ResourceManager::LoadShaderFromSource(BATCH_VERT_SRC, BATCH_FRAG_SRC, "sprite_batch");
    ResourceManager::GetShader("sprite_batch").Use();
    ResourceManager::GetShader("sprite_batch").SetInteger("image", 0);
    Batch = std::unique_ptr<SpriteBatch>(new SpriteBatch(ResourceManager::GetShader("sprite_batch")));
    
    std::cout << " | Terrain" << std::flush;
    GameTerrain = std::unique_ptr<Terrain>(new Terrain(WORLD_W, WORLD_H));
    GameTerrain->Init();
    
    std::cout << " | Players" << std::flush;
    Players[0] = std::unique_ptr<GameObject>(new GameObject(
        glm::vec2(200.0f, 1100.0f), glm::vec2(40.0f, 40.0f),
        ResourceManager::GetTexture("player1")));
    Players[1] = std::unique_ptr<GameObject>(new GameObject(
        glm::vec2(2800.0f, 1100.0f), glm::vec2(40.0f, 59.0f),
        ResourceManager::GetTexture("player2")));
    
    m_camera = std::unique_ptr<Camera>(new Camera(Width, Height, WORLD_W, WORLD_H));
    m_camera->setCenter(1500.0f, 700.0f);
    
    std::cout << " | Particles" << std::flush;
    
    GLubyte imgTrail[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        imgTrail[i*4+0] = 255;
        imgTrail[i*4+1] = 200;
        imgTrail[i*4+2] = 50;
        imgTrail[i*4+3] = 255;
    }
    ResourceManager::GetTexture("trail").Generate(4, 4, imgTrail);
    
    m_trailEmitter = std::unique_ptr<ParticleEmitter>(new ParticleEmitter(200));
    
    std::cout << " | Font" << std::flush;
    Text = std::unique_ptr<TextRenderer>(new TextRenderer(Width, Height));
    Text->Load((g_assetBase + "fonts/wqy-microhei.ttc"), 24);
    
    std::cout << " | Network" << std::flush;
    auto& net = ddt::NetworkClient::Instance();
    net.connect("ws://" + ServerAddr + "/ddt/game");
    net.enableAutoReconnect("ws://" + ServerAddr + "/ddt/game");
    State = GAME_LOGIN;
    StatusText = "LOGIN TO PLAY";
    std::cout << " | OK" << std::endl;
    
    std::cout << C_BOLD << "\n========================================"
              << "\n  DDT - 弹弹堂"
              << "\n========================================" << C_RESET << std::endl;
    std::cout << C_CYAN << "[L] Login  [J] Join Room  [ESC] Quit" << C_RESET << std::endl;
}

GameObject* Game::myPlayer() {
    if (MyIndex < 0) return Players[0].get();
    return Players[MyIndex].get();
}

GameObject* Game::opponentPlayer() {
    if (MyIndex < 0) return Players[1].get();
    return Players[1 - MyIndex].get();
}

int Game::myHP() {
    if (MyIndex < 0) return HP[0];
    return HP[MyIndex];
}

int Game::opponentHP() {
    if (MyIndex < 0) return HP[1];
    return HP[1 - MyIndex];
}

std::string Game::formatInt(int val) {
    if (val < 0) return "-" + std::to_string(-val);
    return std::to_string(val);
}

void Game::ProcessInput(GLfloat dt) {
    ImGuiIO& io = ImGui::GetIO();

    if (State == GAME_MENU) {
        if (Keys[GLFW_KEY_L] && !io.WantCaptureKeyboard) {
            Keys[GLFW_KEY_L] = GL_FALSE;
            ddt::NetworkClient::Instance().sendRoomList();
        }
        return;
    }
    
    if (State != GAME_PLAYING) return;
    if (!IsMyTurn) return;
    if (io.WantCaptureKeyboard) return;
    
    // 角度系统：固定基础范围 [20, 65]，不参与坡度计算
    // 坡度由服务端权威计算，客户端只发送基础角度
    {
        m_effectiveAngleMin = 20;
        m_effectiveAngleMax = 65;
        if (CurrentAngle < m_effectiveAngleMin) CurrentAngle = m_effectiveAngleMin;
        if (CurrentAngle > m_effectiveAngleMax) CurrentAngle = m_effectiveAngleMax;
    }
    
    float now = glfwGetTime();
    
    if (Keys[GLFW_KEY_W]) {
        if (CurrentAngle < m_effectiveAngleMax) CurrentAngle += 1;
    }
    if (Keys[GLFW_KEY_S]) {
        if (CurrentAngle > m_effectiveAngleMin) CurrentAngle -= 1;
    }
    
    // A/D: 移动（节流 50ms, 并且本地拦截超出 200 限额的移动）
    // 修改：玩家开火后，立刻剥夺移动权限
    if (!m_hasShot && now - m_lastMoveTime > 0.05f) {
        float step = 5.0f; // 单步移动距离
        if (Keys[GLFW_KEY_A]) {
            auto* me = myPlayer();
            // 限制本回合移动距离不超过 200
            if (me->Position.x > 0 && m_moveUsed + step <= m_maxMovePerTurn) {
                float dx = -step;
                me->Position.x += dx;
                Directions[MyIndex] = 0; 
                if (me->Position.x < 0) me->Position.x = 0;
                ddt::NetworkClient::Instance().sendMove(dx);
                m_moveUsed += step; // 累加本地已行走距离
                m_lastMoveTime = now;
            }
        }
        if (Keys[GLFW_KEY_D]) {
            auto* me = myPlayer();
            // 限制本回合移动距离不超过 200
            if (me->Position.x < WORLD_W - me->Size.x && m_moveUsed + step <= m_maxMovePerTurn) {
                float dx = step;
                me->Position.x += dx;
                Directions[MyIndex] = 1; 
                if (me->Position.x > WORLD_W - me->Size.x)
                    me->Position.x = WORLD_W - me->Size.x;
                ddt::NetworkClient::Instance().sendMove(dx);
                m_moveUsed += step; // 累加本地已行走距离
                m_lastMoveTime = now;
            }
        }
    }
    
    bool isPressing = Keys[GLFW_KEY_SPACE];
    
    if (isPressing && !m_hasShot) {
        if (!IsCharging) {
            IsCharging = true;
            Power = 0.0f;
        }
        Power += 80.0f * dt;
        if (Power > 100.0f) Power = 100.0f;
    } else if (!isPressing && IsCharging && Power > 0 && !m_hasShot) {
        m_hasShot = true;
    
        // 完美方案：直接发送 CurrentAngle（基准角度），服务端做有效范围校验，服务端计算物理坡度！
        ddt::NetworkClient::Instance().sendShoot(CurrentAngle, Power, m_useFlyItem);
        if(m_useFlyItem) { m_flyCooldown = 2; m_useFlyItem = false; }
        
        IsCharging = false;
        Power = 0.0f;
    }
    
    // F 键切换纸飞机
    if (Keys[GLFW_KEY_F] && !m_hasShot) {
        Keys[GLFW_KEY_F] = GL_FALSE;
        if (m_flyCooldown <= 0) {
            m_useFlyItem = !m_useFlyItem;
        }
    }
    
    if (Keys[GLFW_KEY_P] && !m_hasShot) {
        Keys[GLFW_KEY_P] = GL_FALSE;
        if (!IsCharging) {
            m_hasShot = true;
            std::cout << C_YELLOW << "> Pass turn!" << C_RESET << std::endl;
            ddt::NetworkClient::Instance().sendPass();
        }
    }
}

static float s_pingTimer = 0.0f;

void Game::Update(GLfloat dt) {
    processNetworkMessages();

    auto& net = ddt::NetworkClient::Instance();
    
    // 自动重连（问题13修复）：断线后以指数退避重连
    if (State != GAME_LOGIN && !net.isConnected()) {
        if (net.shouldAutoReconnect()) {
            static int s_retryCount = 0;
            static double s_retryTimer = 0.0;
            s_retryTimer += dt;
            int delaySec = std::min(1 << s_retryCount, 30);
            if (s_retryTimer >= delaySec) {
                s_retryTimer = 0;
                s_retryCount++;
                std::cout << C_YELLOW << "< Reconnect attempt #" << s_retryCount
                          << " (next in " << std::min(1 << s_retryCount, 30) << "s)"
                          << C_RESET << std::endl;
                if (net.connect(net.getReconnectUrl())) {
                    s_retryCount = 0;
                    // 重连成功后自动重新登录
                    if (!MyName.empty()) {
                        net.sendLogin(MyName, m_password);
                    }
                    return;
                }
            }
            // 重连期间保持当前游戏状态，不立即回到 LOGIN
            return;
        }
        State = GAME_LOGIN;
        StatusText = "DISCONNECTED - PLEASE RECONNECT";
        CurrentRoomId = 0;
        m_myReady = false;
        m_loggedIn = false;
        std::cout << C_RED << "< Disconnected from server!" << C_RESET << std::endl;
        return;
    }
    
    bool projWasActive = projectile.IsActive();
    projectile.Update(dt);
    
    if (projWasActive && !projectile.IsActive() && m_pendingHit.active) {
        // 纸飞机不打坑、不爆炸
        if (!m_projIsFly) {
            if (GameTerrain && m_pendingHit.hit_x > 0) {
                GameTerrain->RemoveCircle(m_pendingHit.hit_x, m_pendingHit.hit_y, 30.0f);
            }
    
            if (m_pendingHit.hit_x > 0) {
                m_explosions.push_back({glm::vec2(m_pendingHit.hit_x, m_pendingHit.hit_y), 0.0f, 0, true});
            }
        }
    
        HP[0] = m_pendingHit.hp0;
        HP[1] = m_pendingHit.hp1;
        Players[0]->Position = m_pendingHit.pos0;
        Players[1]->Position = m_pendingHit.pos1;
    
        if (m_pendingHit.hit_player && m_pendingHit.damage > 0) {
            int hitIdx = -1;
            if (MyIndex >= 0) {
                hitIdx = (m_pendingHit.hit_player_id == MyId) ? MyIndex : (1 - MyIndex);
            }
            if (hitIdx >= 0 && Players[hitIdx]) {
                std::string dmgText;
                glm::vec3 dmgColor;
                if (m_pendingHit.damage_type == ddt::ShootResultNotify::CRITICAL) {
                    dmgText = "CRIT -" + std::to_string(m_pendingHit.damage);
                    dmgColor = glm::vec3(1.0f, 0.8f, 0.0f);
                } else if (m_pendingHit.damage_type == ddt::ShootResultNotify::BLOCK) {
                    dmgText = "Block -" + std::to_string(m_pendingHit.damage);
                    dmgColor = glm::vec3(0.3f, 0.8f, 1.0f);
                } else {
                    dmgText = "-" + std::to_string(m_pendingHit.damage);
                    dmgColor = glm::vec3(1.0f, 0.3f, 0.3f);
                }
                m_damageFloats.push_back({
                    Players[hitIdx]->Position + glm::vec2(0.0f, -20.0f),
                    dmgText, 0.0f, dmgColor, true
                });
            }
        }
        m_pendingHit.active = false;
    }
    
    if (m_rightMousePressed) {
        if (!m_cameraDragging) {
            m_cameraDragging = true;
            m_dragLastX = m_mouseX;
            m_dragLastY = m_mouseY;
        } else {
            double dx = m_mouseX - m_dragLastX;
            double dy = m_mouseY - m_dragLastY;
            if (dx != 0 || dy != 0) {
                m_camera->setCenter(
                    m_camera->getCamX() - (float)dx,
                    m_camera->getCamY() - (float)dy);
                m_dragLastX = m_mouseX;
                m_dragLastY = m_mouseY;
                m_cameraMode = CAM_MANUAL;
                m_manualTimer = 0;
            }
        }
    } else {
        m_cameraDragging = false;
    }
    
    updateCamera(dt);
    
    if (State == GAME_PLAYING) {
        for (int i = 0; i < 2; i++) {
            if (Players[i] && GameTerrain) {
                float px = Players[i]->Position.x + Players[i]->Size.x * 0.5f;
                float py = Players[i]->Position.y + Players[i]->Size.y;
    
                if (!GameTerrain->IsSolid(px, py)) {
                    Players[i]->Velocity.y += 1500.0f * dt;
                    Players[i]->Position.y += Players[i]->Velocity.y * dt;
                } else {
                    Players[i]->Velocity.y = 0.0f;
                    while (GameTerrain->IsSolid(px, Players[i]->Position.y + Players[i]->Size.y - 1.0f)) {
                        Players[i]->Position.y -= 1.0f;
                    }
                }
            }
        }
    }
    
    for (auto& ex : m_explosions) {
        if (!ex.active) continue;
        ex.timer += dt;
        ex.frame = (int)(ex.timer / 0.08f);
        if (ex.frame >= 4) ex.active = false;
    }
    
    for (auto& df : m_damageFloats) {
        if (!df.active) continue;
        df.timer += dt;
        df.pos.y -= 40.0f * dt;
        if (df.timer > 1.5f) df.active = false;
    }
    
    if (projectile.IsActive()) {
        glm::vec2 pos = projectile.GetCurrentPos();
        for (int i = 0; i < 2; i++) {
            float ox = (rand() % 6 - 3) * 0.5f;
            float oy = (rand() % 6 - 3) * 0.5f;
            m_trailEmitter->emit(
                glm::vec2(pos.x + ox, pos.y + oy),
                glm::vec2((rand() % 20 - 10) * 0.5f, (rand() % 20 - 10) * 0.5f),
                glm::vec4(1.0f, 0.8f, 0.2f, 0.7f),
                0.4f + (rand() % 10) * 0.05f
            );
        }
    }
    m_trailEmitter->update(dt);
    
    s_pingTimer += dt;
    if (s_pingTimer > 30.0f) {
        ddt::NetworkClient::Instance().sendHeartbeat();
        s_pingTimer = 0.0f;
    }
}

void Game::processNetworkMessages() {
    static GameNetwork s_network(*this);
    s_network.processMessages();
}

void Game::Render() {
    static GameRenderer s_renderer(*this);
    s_renderer.Render();
}

void Game::RenderImGui() {
    static GameHUD s_hud(*this);
    s_hud.RenderAll();
}

void Game::updateCamera(float dt) {
    if (!m_camera) return;

    switch (m_cameraMode) {
    case CAM_INTRO: {
        m_introTimer += dt;
        if (m_introPhase == 0) {
            m_camera->panTo(Players[0]->Position.x, Players[0]->Position.y, 600.0f);
            m_introPhase = 1;
            m_introTimer = 0;
        } else if (m_introPhase == 1) {
            if (m_introTimer > 1.5f && !m_camera->isPanning()) {
                m_introPhase = 2;
                m_introTimer = 0;
            }
        } else if (m_introPhase == 2) {
            m_camera->panTo(Players[1]->Position.x, Players[1]->Position.y, 600.0f);
            m_introPhase = 3;
            m_introTimer = 0;
        } else if (m_introPhase == 3) {
            if (m_introTimer > 1.5f && !m_camera->isPanning()) {
                m_cameraMode = CAM_FOLLOW_TURN;
                GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
                m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
            }
        }
        break;
    }
    
    case CAM_FOLLOW_TURN: {
        GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
        m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 600.0f);
        break;
    }
    
    case CAM_FOLLOW_PROJ: {
        if (projectile.IsActive()) {
            glm::vec2 pos = projectile.GetCurrentPos();
            m_camera->setCenter(pos.x, pos.y);
        } else {
        }
        break;
    }
    
    case CAM_MANUAL: {
        m_manualTimer += dt;
        if (m_manualTimer > 3.0f) {
            if (projectile.IsActive()) {
                m_cameraMode = CAM_FOLLOW_PROJ;
            } else {
                m_cameraMode = CAM_FOLLOW_TURN;
                GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
                m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
            }
        }
        break;
    }
    
    case CAM_FREE:
    default:
        break;
    }
    
    m_camera->update(dt);
}

void Game::Shutdown() {
    ResourceManager::Clear();
    ddt::NetworkClient::Instance().disconnect();
}