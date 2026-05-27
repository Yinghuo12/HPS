#include "game.h"
#include "network_client.h"
#include "ddt.pb.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>
#include <cmath>
#include <fstream>

static std::string g_assetBase;

static void findAssetBase() {
    // Try multiple common locations
    const char* candidates[] = {
        "assets/",                    // running from ddt_client/
        "ddt_client/assets/",         // running from sylar root
        "../ddt_client/assets/",      // running from bin/
        "../../ddt_client/assets/",   // running from build/
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

// ANSI 颜色
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"
#define C_RESET   "\033[0m"

// 内嵌 shader 源码（不依赖外部文件）
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
    , Renderer(nullptr), Batch(nullptr), GameTerrain(nullptr)
    , Text(nullptr), m_camera(nullptr)
    , CurrentAngle(60), OpponentAngle(60)
    , Power(0.0f), IsCharging(false)
    , IsMyTurn(false), Wind(0.0f), MyId(0)
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
    , m_lastMoveTime(0.0f), m_imguiCharging(false)
    , m_loggedIn(false)
    , m_password()
    , m_currentChannel(3)
    , m_chatScrollToBottom(false) {
    memset(Keys, 0, sizeof(Keys));
    memset(m_chatInput, 0, sizeof(m_chatInput));
    Players[0] = nullptr;
    Players[1] = nullptr;
    HP[0] = 100; HP[1] = 100;
    Directions[0] = 1; Directions[1] = 0;
}

Game::~Game() {
    delete Players[0];
    delete Players[1];
    delete Renderer;
    delete Batch;
    delete m_trailEmitter;
    delete GameTerrain;
    delete Text;
    ResourceManager::Clear();
    ddt::NetworkClient::Instance().disconnect();
}

void Game::Init() {
    findAssetBase();

    ResourceManager::LoadShaderFromSource(SPRITE_VERT_SRC, SPRITE_FRAG_SRC, "sprite");

    glm::mat4 projection = glm::ortho(0.0f, static_cast<GLfloat>(Width),
                                       static_cast<GLfloat>(Height), 0.0f, -1.0f, 1.0f);
    ResourceManager::GetShader("sprite").Use();
    ResourceManager::GetShader("sprite").SetInteger("image", 0);
    ResourceManager::GetShader("sprite").SetMatrix4("projection", projection);

    // DDT 原版角色贴图 (role.bmp → player1, role2.bmp → player2)
    ResourceManager::LoadTexture((g_assetBase + "role.png").c_str(), true, "player1");
    ResourceManager::LoadTexture((g_assetBase + "role_r.png").c_str(), true, "player1_r");
    ResourceManager::LoadTexture((g_assetBase + "role2.png").c_str(), true, "player2");
    ResourceManager::LoadTexture((g_assetBase + "role2_r.png").c_str(), true, "player2_r");

    // DDT 武器 - Player1 (tri_darts)
    ResourceManager::LoadTexture((g_assetBase + "tri_darts.png").c_str(), true, "proj_p1");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_r.png").c_str(), true, "proj_p1_r");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_bomb.png").c_str(), true, "proj_p1_bomb");
    ResourceManager::LoadTexture((g_assetBase + "tri_darts_bomb_r.png").c_str(), true, "proj_p1_bomb_r");

    // DDT 武器 - Player2 (ice_cream)
    ResourceManager::LoadTexture((g_assetBase + "ice_cream.png").c_str(), true, "proj_p2");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_r.png").c_str(), true, "proj_p2_r");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_bomb.png").c_str(), true, "proj_p2_bomb");
    ResourceManager::LoadTexture((g_assetBase + "ice_cream_bomb_r.png").c_str(), true, "proj_p2_bomb_r");

    // DDT 爆炸帧 (bow0-3)
    ResourceManager::LoadTexture((g_assetBase + "bow0.png").c_str(), true, "explosion_0");
    ResourceManager::LoadTexture((g_assetBase + "bow1.png").c_str(), true, "explosion_1");
    ResourceManager::LoadTexture((g_assetBase + "bow2.png").c_str(), true, "explosion_2");
    ResourceManager::LoadTexture((g_assetBase + "bow3.png").c_str(), true, "explosion_3");

    // DDT 特效
    ResourceManager::LoadTexture((g_assetBase + "fly.png").c_str(), true, "fly");
    ResourceManager::LoadTexture((g_assetBase + "flyAttack.png").c_str(), true, "fly_attack");
    ResourceManager::LoadTexture((g_assetBase + "flyAttack_r.png").c_str(), true, "fly_attack_r");
    ResourceManager::LoadTexture((g_assetBase + "bomb.png").c_str(), true, "bomb_icon");

    // DDT 背景
    ResourceManager::LoadTexture((g_assetBase + "bg_rainbow.png").c_str(), false, "sky");
    ResourceManager::LoadTexture((g_assetBase + "bg_ghost.png").c_str(), false, "sky_ghost");

    // 程序化贴图（保留）
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

    Renderer = new SpriteRenderer(ResourceManager::GetShader("sprite"));

    // Batch renderer
    ResourceManager::LoadShaderFromSource(BATCH_VERT_SRC, BATCH_FRAG_SRC, "sprite_batch");
    ResourceManager::GetShader("sprite_batch").Use();
    ResourceManager::GetShader("sprite_batch").SetInteger("image", 0);
    Batch = new SpriteBatch(ResourceManager::GetShader("sprite_batch"));

    GameTerrain = new Terrain(WORLD_W, WORLD_H);
    GameTerrain->Init();

    Players[0] = new GameObject(
        glm::vec2(200.0f, 1100.0f), glm::vec2(40.0f, 40.0f),
        ResourceManager::GetTexture("player1"));
    Players[1] = new GameObject(
        glm::vec2(2800.0f, 1100.0f), glm::vec2(40.0f, 59.0f),
        ResourceManager::GetTexture("player2"));

    m_camera = new Camera(Width, Height, WORLD_W, WORLD_H);
    m_camera->setCenter(1500.0f, 700.0f);

    // Trail particle texture (4x4 yellow)
    GLubyte imgTrail[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        imgTrail[i*4+0] = 255;
        imgTrail[i*4+1] = 200;
        imgTrail[i*4+2] = 50;
        imgTrail[i*4+3] = 255;
    }
    ResourceManager::GetTexture("trail").Generate(4, 4, imgTrail);

    m_trailEmitter = new ParticleEmitter(200);

    Text = new TextRenderer(Width, Height);
    Text->Load((g_assetBase + "fonts/wqy-microhei.ttc"), 24);

    ddt::NetworkClient::Instance().connect("ws://" + ServerAddr + "/ddt/game");
    State = GAME_LOGIN;
    StatusText = "LOGIN TO PLAY";

    std::cout << C_BOLD << "\n========================================"
              << "\n  DDT - 弹弹堂"
              << "\n========================================" << C_RESET << std::endl;
    std::cout << C_CYAN << "[L] Login  [J] Join Room  [ESC] Quit" << C_RESET << std::endl;
}

// ---- 辅助方法 ----

GameObject* Game::myPlayer() {
    if (MyIndex < 0) return Players[0];
    return Players[MyIndex];
}

GameObject* Game::opponentPlayer() {
    if (MyIndex < 0) return Players[1];
    return Players[1 - MyIndex];
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

// ---- 输入处理 ----

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

    float now = glfwGetTime();

    // W/S: 调整角度 (20-65)
    if (Keys[GLFW_KEY_W]) {
        if (CurrentAngle < 65) CurrentAngle += 1;
    }
    if (Keys[GLFW_KEY_S]) {
        if (CurrentAngle > 20) CurrentAngle -= 1;
    }

    // A/D: 移动（节流 50ms, 小步快跑更平滑）
    if (now - m_lastMoveTime > 0.05f) {
        if (Keys[GLFW_KEY_A]) {
            auto* me = myPlayer();
            if (me->Position.x > 0) {
                float dx = -5.0f;
                me->Position.x += dx;
                if (me->Position.x < 0) me->Position.x = 0;
                ddt::NetworkClient::Instance().sendMove(dx);
                m_lastMoveTime = now;
            }
        }
        if (Keys[GLFW_KEY_D]) {
            auto* me = myPlayer();
            if (me->Position.x < WORLD_W - me->Size.x) {
                float dx = 5.0f;
                me->Position.x += dx;
                if (me->Position.x > WORLD_W - me->Size.x)
                    me->Position.x = WORLD_W - me->Size.x;
                ddt::NetworkClient::Instance().sendMove(dx);
                m_lastMoveTime = now;
            }
        }
    }

    // 空格：蓄力/发射
    if (Keys[GLFW_KEY_SPACE]) {
        if (!IsCharging) {
            IsCharging = true;
            Power = 0.0f;
        }
        Power += 80.0f * dt;
        if (Power > 100.0f) Power = 100.0f;
    } else {
        if (IsCharging && Power > 0) {
            std::cout << C_YELLOW << "> Shoot! angle=" << CurrentAngle
                      << " power=" << (int)Power << C_RESET << std::endl;
            ddt::NetworkClient::Instance().sendShoot(CurrentAngle, Power);
            IsCharging = false;
            Power = 0.0f;
        }
    }
}

// ---- 更新 ----

static float s_pingTimer = 0.0f;

void Game::Update(GLfloat dt) {
    processNetworkMessages();

    // Disconnect detection
    if (State != GAME_LOGIN && !ddt::NetworkClient::Instance().isConnected()) {
        State = GAME_LOGIN;
        StatusText = "DISCONNECTED - PLEASE RECONNECT";
        CurrentRoomId = 0;
        m_myReady = false;
        m_loggedIn = false;
        std::cout << C_RED << "< Disconnected from server!" << C_RESET << std::endl;
        return;
    }

    projectile.Update(dt);

    // Right-click camera drag
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

    // Update explosions (4 frames, ~0.08s each = 0.32s total)
    for (auto& ex : m_explosions) {
        if (!ex.active) continue;
        ex.timer += dt;
        ex.frame = (int)(ex.timer / 0.08f);
        if (ex.frame >= 4) ex.active = false;
    }

    // Update damage floats (drift up, fade out over 1.5s)
    for (auto& df : m_damageFloats) {
        if (!df.active) continue;
        df.timer += dt;
        df.pos.y -= 40.0f * dt;
        if (df.timer > 1.5f) df.active = false;
    }

    // Emit trail particles when projectile is active
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

    // ImGui 蓄力：每帧递增
    if (m_imguiCharging) {
        Power += 80.0f * dt;
        if (Power > 100.0f) Power = 100.0f;
    }

    // Keepalive: 每 30s 发一次 ping
    s_pingTimer += dt;
    if (s_pingTimer > 30.0f) {
        ddt::NetworkClient::Instance().sendPing();
        s_pingTimer = 0.0f;
    }
}

void Game::processNetworkMessages() {
    while (ddt::NetworkClient::Instance().hasMessages()) {
        auto msg = ddt::NetworkClient::Instance().pollMessage();
        if (!msg) break;

        switch (msg->payload_case()) {
            case ddt::GameMessage::kLoginResponse: {
                auto& resp = msg->login_response();
                if (resp.ok()) {
                    MyId = resp.id();
                    m_loggedIn = true;
                    State = GAME_MENU;
                    StatusText = "SELECT OR CREATE A ROOM";
                    std::cout << C_GREEN << "< Login OK! ID=" << MyId << C_RESET << std::endl;
                    // Auto-request room list
                    ddt::NetworkClient::Instance().sendRoomList();
                } else {
                    std::cout << C_RED << "< Login failed: " << resp.msg() << C_RESET << std::endl;
                    StatusText = "LOGIN FAILED: " + resp.msg();
                }
                break;
            }
            case ddt::GameMessage::kJoinRoomResponse: {
                auto& resp = msg->join_room_response();
                if (resp.ok()) {
                    CurrentRoomId = resp.room_id();
                    m_myReady = false;
                    std::cout << C_GREEN << "< Joined room #" << resp.room_id()
                              << ", waiting..." << C_RESET << std::endl;
                    State = GAME_WAITING;
                    StatusText = "WAITING IN ROOM...";
                    // Request room list to get updated info
                    ddt::NetworkClient::Instance().sendRoomList();
                } else {
                    std::cout << C_RED << "< Join failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomReadyNotify: {
                auto& nty = msg->room_ready_notify();
                auto& p1 = nty.player1();
                auto& p2 = nty.player2();

                Players[0]->Position = glm::vec2(p1.x(), p1.y());
                Players[1]->Position = glm::vec2(p2.x(), p2.y());
                HP[0] = p1.hp();
                HP[1] = p2.hp();
                Directions[0] = p1.direction();
                Directions[1] = p2.direction();
                Wind = nty.wind();

                if (p1.id() == MyId) {
                    MyIndex = 0;
                    CurrentAngle = p1.angle();
                    OpponentAngle = p2.angle();
                } else {
                    MyIndex = 1;
                    CurrentAngle = p2.angle();
                    OpponentAngle = p1.angle();
                }

                IsMyTurn = (nty.first_turn_id() == MyId);
                State = GAME_PLAYING;

                // Trigger intro camera sequence
                m_cameraMode = CAM_INTRO;
                m_introPhase = 0;
                m_introTimer = 0;
                m_camera->snapTo(Players[0]->Position.x, Players[0]->Position.y);

                std::cout << C_BOLD << C_GREEN
                          << "\n======== GAME START ========"
                          << "\n  P1(ID=" << p1.id() << " " << p1.name()
                          << ") vs P2(ID=" << p2.id() << " " << p2.name() << ")"
                          << "\n  P1 pos=(" << p1.x() << "," << p1.y() << ")"
                          << "  P2 pos=(" << p2.x() << "," << p2.y() << ")"
                          << "\n  Wind: " << Wind
                          << "  First turn: " << (IsMyTurn ? "YOU" : "OPPONENT")
                          << "\n=============================" << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kTurnStartNotify: {
                auto& nty = msg->turn_start_notify();
                Wind = nty.wind();
                TurnNumber = nty.turn_number();
                IsMyTurn = (nty.turn_player_id() == MyId);

                std::cout << C_CYAN << "< [Turn " << TurnNumber << "] "
                          << (IsMyTurn ? C_BOLD "YOUR TURN" : "OPPONENT TURN")
                          << "  Wind: " << Wind << C_RESET << std::endl;

                if (IsMyTurn) {
                    StatusText = "YOUR TURN!";
                } else {
                    StatusText = "OPPONENT TURN...";
                }

                // Camera: follow current turn player
                m_cameraMode = CAM_FOLLOW_TURN;
                GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
                m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
                break;
            }
            case ddt::GameMessage::kShootResultNotify: {
                auto& nty = msg->shoot_result_notify();

                m_lastShooterIdx = IsMyTurn ? MyIndex : (1 - MyIndex);

                HP[0] = nty.updated_player1().hp();
                HP[1] = nty.updated_player2().hp();
                Players[0]->Position = glm::vec2(nty.updated_player1().x(),
                                                  nty.updated_player1().y());
                Players[1]->Position = glm::vec2(nty.updated_player2().x(),
                                                  nty.updated_player2().y());

                if (MyIndex >= 0) {
                    if (MyIndex == 0) {
                        OpponentAngle = nty.updated_player2().angle();
                    } else {
                        OpponentAngle = nty.updated_player1().angle();
                    }
                }

                std::vector<TrajectoryPoint> points;
                for (int i = 0; i < nty.points_size(); i++) {
                    auto& p = nty.points(i);
                    points.push_back({p.x(), p.y(), p.t()});
                }
                projectile.Start(points);

                // Camera: follow projectile
                m_cameraMode = CAM_FOLLOW_PROJ;

                if (GameTerrain && nty.hit_x() > 0) {
                    GameTerrain->RemoveCircle(nty.hit_x(), nty.hit_y(), 30.0f);
                }

                // Explosion at hit point
                if (nty.hit_x() > 0) {
                    m_explosions.push_back({
                        glm::vec2(nty.hit_x(), nty.hit_y()),
                        0.0f, 0, true
                    });
                }

                // Damage float on hit player
                if (nty.hit_player() && nty.damage() > 0) {
                    int hitIdx = -1;
                    if (MyIndex >= 0) {
                        hitIdx = (nty.hit_player_id() == MyId) ? MyIndex : (1 - MyIndex);
                    }
                    if (hitIdx >= 0 && Players[hitIdx]) {
                        std::string dmgText;
                        glm::vec3 dmgColor;
                        if (nty.damage_type() == ddt::ShootResultNotify::CRITICAL) {
                            dmgText = "CRIT -" + std::to_string(nty.damage());
                            dmgColor = glm::vec3(1.0f, 0.8f, 0.0f);  // gold
                        } else if (nty.damage_type() == ddt::ShootResultNotify::BLOCK) {
                            dmgText = "Block -" + std::to_string(nty.damage());
                            dmgColor = glm::vec3(0.3f, 0.8f, 1.0f);  // cyan
                        } else {
                            dmgText = "-" + std::to_string(nty.damage());
                            dmgColor = glm::vec3(1.0f, 0.3f, 0.3f);  // red
                        }
                        m_damageFloats.push_back({
                            Players[hitIdx]->Position + glm::vec2(0.0f, -20.0f),
                            dmgText, 0.0f, dmgColor, true
                        });
                    }
                }

                std::cout << C_RED << "< Shoot result: hit=" << nty.hit_player()
                          << " damage=" << nty.damage()
                          << "  P1 HP:" << HP[0]
                          << "  P2 HP:" << HP[1]
                          << "  traj_pts=" << nty.points_size()
                          << C_RESET << std::endl;
                if (nty.points_size() > 0) {
                    auto& p0 = nty.points(0);
                    auto& pN = nty.points(nty.points_size()-1);
                    std::cout << "  traj start=(" << p0.x() << "," << p0.y()
                              << ") t=" << p0.t()
                              << " end=(" << pN.x() << "," << pN.y()
                              << ") t=" << pN.t() << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kMoveNotify: {
                auto& nty = msg->move_notify();
                if (MyIndex >= 0) {
                    int idx = (nty.player_id() == MyId) ? MyIndex : (1 - MyIndex);
                    Players[idx]->Position.x = nty.new_x();
                }
                break;
            }
            case ddt::GameMessage::kGameOverNotify: {
                auto& nty = msg->game_over_notify();
                State = GAME_OVER;
                bool won = (nty.winner_id() == MyId);
                StatusText = won ? "YOU WIN!" : "YOU LOSE!";

                std::cout << C_BOLD << (won ? C_GREEN : C_RED)
                          << "\n======== GAME OVER ========"
                          << "\n  " << StatusText
                          << "\n  Reason: " << nty.reason()
                          << "\n===========================" << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kOpponentLeftNotify: {
                std::cout << C_RED << "< Opponent left!" << C_RESET << std::endl;
                State = GAME_OVER;
                StatusText = "OPPONENT LEFT!";
                break;
            }
            case ddt::GameMessage::kErrorNotify: {
                auto& nty = msg->error_notify();
                std::cout << C_RED << "< Error " << nty.code()
                          << ": " << nty.msg() << C_RESET << std::endl;
                if (nty.code() == 409) {
                    StatusText = "KICKED - Account logged in elsewhere";
                    State = GAME_LOGIN;
                    CurrentRoomId = 0;
                }
                break;
            }
            case ddt::GameMessage::kServerShutdownNotify: {
                auto& nty = msg->server_shutdown_notify();
                StatusText = "SERVER SHUTDOWN - " + nty.reason();
                State = GAME_LOGIN;
                CurrentRoomId = 0;
                m_myReady = false;
                std::cout << C_RED << "< Server shutdown: " << nty.reason()
                          << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kRegisterResponse: {
                auto& resp = msg->register_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Register OK! Account ID=" << resp.id() << C_RESET << std::endl;
                    StatusText = "REGISTER OK! NOW LOGIN";
                } else {
                    std::cout << C_RED << "< Register failed: " << resp.msg() << C_RESET << std::endl;
                    StatusText = "REGISTER FAILED: " + resp.msg();
                }
                break;
            }
            case ddt::GameMessage::kChatNotify: {
                auto& nty = msg->chat_notify();
                ChatMsg cm;
                cm.channel = nty.channel();
                cm.sender_id = nty.sender_id();
                cm.sender_name = nty.sender_name();
                cm.message = nty.message();
                cm.timestamp = nty.timestamp();
                m_chatMessages.push_back(cm);
                if (m_chatMessages.size() > 500)
                    m_chatMessages.erase(m_chatMessages.begin(), m_chatMessages.begin() + 100);
                m_chatScrollToBottom = true;
                break;
            }
            case ddt::GameMessage::kChatHistoryResponse: {
                auto& resp = msg->chat_history_response();
                for (int i = 0; i < resp.messages_size(); i++) {
                    auto& nty = resp.messages(i);
                    ChatMsg cm;
                    cm.channel = nty.channel();
                    cm.sender_id = nty.sender_id();
                    cm.sender_name = nty.sender_name();
                    cm.message = nty.message();
                    cm.timestamp = nty.timestamp();
                    m_chatMessages.push_back(cm);
                }
                m_chatScrollToBottom = true;
                break;
            }
            case ddt::GameMessage::kFriendAddResponse: {
                auto& resp = msg->friend_add_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Friend added: " << resp.friend_name() << C_RESET << std::endl;
                } else {
                    std::cout << C_RED << "< Friend add failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomListResponse: {
                auto& resp = msg->room_list_response();
                RoomList.clear();
                for (int i = 0; i < resp.rooms_size(); i++) {
                    auto& r = resp.rooms(i);
                    RoomInfoClient ri;
                    ri.room_id = r.room_id();
                    ri.room_name = r.room_name();
                    ri.player_count = r.player_count();
                    ri.max_players = r.max_players();
                    ri.game_started = r.game_started();
                    for (int j = 0; j < r.players_size(); j++) {
                        auto& s = r.players(j);
                        RoomSlot slot;
                        slot.player_id = s.player_id();
                        slot.player_name = s.player_name();
                        slot.team = s.team();
                        slot.ready = s.ready();
                        ri.players.push_back(slot);
                    }
                    RoomList.push_back(ri);
                }
                break;
            }
            case ddt::GameMessage::kCreateRoomResponse: {
                auto& resp = msg->create_room_response();
                if (resp.ok()) {
                    CurrentRoomId = resp.room_id();
                    State = GAME_WAITING;
                    m_myReady = false;
                    StatusText = "ROOM CREATED, WAITING...";
                    std::cout << C_GREEN << "< Created room #" << resp.room_id() << C_RESET << std::endl;
                } else {
                    StatusText = "CREATE FAILED: " + resp.msg();
                    std::cout << C_RED << "< Create room failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomUpdateNotify: {
                auto& nty = msg->room_update_notify();
                auto& r = nty.room_info();
                // Update our room in the list
                bool found = false;
                for (auto& ri : RoomList) {
                    if (ri.room_id == r.room_id()) {
                        ri.room_name = r.room_name();
                        ri.player_count = r.player_count();
                        ri.max_players = r.max_players();
                        ri.game_started = r.game_started();
                        ri.players.clear();
                        for (int j = 0; j < r.players_size(); j++) {
                            auto& s = r.players(j);
                            RoomSlot slot;
                            slot.player_id = s.player_id();
                            slot.player_name = s.player_name();
                            slot.team = s.team();
                            slot.ready = s.ready();
                            ri.players.push_back(slot);
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    RoomInfoClient ri;
                    ri.room_id = r.room_id();
                    ri.room_name = r.room_name();
                    ri.player_count = r.player_count();
                    ri.max_players = r.max_players();
                    ri.game_started = r.game_started();
                    for (int j = 0; j < r.players_size(); j++) {
                        auto& s = r.players(j);
                        RoomSlot slot;
                        slot.player_id = s.player_id();
                        slot.player_name = s.player_name();
                        slot.team = s.team();
                        slot.ready = s.ready();
                        ri.players.push_back(slot);
                    }
                    RoomList.push_back(ri);
                }
                break;
            }
            case ddt::GameMessage::kReadyNotify: {
                auto& nty = msg->ready_notify();
                // Update ready state in room list
                for (auto& ri : RoomList) {
                    if (ri.room_id == CurrentRoomId) {
                        for (auto& slot : ri.players) {
                            if (slot.player_id == nty.player_id()) {
                                slot.ready = nty.ready();
                            }
                        }
                    }
                }
                if (nty.player_id() == MyId) {
                    m_myReady = nty.ready();
                }
                break;
            }
            case ddt::GameMessage::kFriendListResponse: {
                // TODO: store friend list for UI
                auto& resp = msg->friend_list_response();
                std::cout << C_GREEN << "< Friends: " << resp.friends_size() << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kSwitchTeamResponse: {
                auto& resp = msg->switch_team_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Switched to "
                              << (resp.team() == 0 ? "RED" : "BLUE") << C_RESET << std::endl;
                } else {
                    std::cout << C_RED << "< Switch failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            default:
                break;
        }
    }
}

// ---- 渲染 ----

void Game::Render() {
    // 登录/菜单阶段只渲染黑色背景
    if (State == GAME_LOGIN || State == GAME_MENU) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // --- 游戏投影（带 camera 偏移） ---
    float camX = m_camera->getCamX();
    float camY = m_camera->getCamY();
    glm::mat4 gameProjection = glm::ortho(
        camX, camX + m_camera->getViewportW(),
        camY + m_camera->getViewportH(), camY,
        -1.0f, 1.0f);

    Batch->Begin(gameProjection);

    // 天空背景（世界大小）
    Batch->Draw(ResourceManager::GetTexture("sky"),
                glm::vec2(0.0f, 0.0f),
                glm::vec2((float)WORLD_W, (float)WORLD_H));

    // 地形用独立渲染（FBO + UV 裁剪），不走 batch
    GameTerrain->Draw(camX, camY, m_camera->getViewportW(), m_camera->getViewportH());

    // 玩家（根据朝向选贴图）
    if (Players[0]) {
        auto& tex0 = (Directions[0] == 1)
            ? ResourceManager::GetTexture("player1_r")
            : ResourceManager::GetTexture("player1");
        Batch->Draw(tex0, Players[0]->Position, Players[0]->Size, 0.0f, glm::vec4(1.0f));
    }
    if (Players[1]) {
        auto& tex1 = (Directions[1] == 1)
            ? ResourceManager::GetTexture("player2_r")
            : ResourceManager::GetTexture("player2");
        Batch->Draw(tex1, Players[1]->Position, Players[1]->Size, 0.0f, glm::vec4(1.0f));
    }

    // 炮管 - Player1
    if (Players[0]) {
        float angleRad = glm::radians((float)(
            (MyIndex == 0) ? CurrentAngle : OpponentAngle));
        bool facingRight = (Directions[0] == 1);
        float bx = facingRight ? Players[0]->Position.x + 25.0f : Players[0]->Position.x + 7.0f;
        float by = Players[0]->Position.y - 10.0f;
        float rot = facingRight ? -angleRad : -(3.14159f - angleRad);
        Batch->Draw(ResourceManager::GetTexture("barrel"),
                    glm::vec2(bx, by), glm::vec2(8.0f, 30.0f), rot);
    }

    // 炮管 - Player2
    if (Players[1]) {
        float angleRad = glm::radians((float)(
            (MyIndex == 1) ? CurrentAngle : OpponentAngle));
        bool facingRight = (Directions[1] == 1);
        float bx = facingRight ? Players[1]->Position.x + 25.0f : Players[1]->Position.x + 7.0f;
        float by = Players[1]->Position.y - 10.0f;
        float rot = facingRight ? -angleRad : -(3.14159f - angleRad);
        Batch->Draw(ResourceManager::GetTexture("barrel"),
                    glm::vec2(bx, by), glm::vec2(8.0f, 30.0f), rot);
    }

    // 弹丸 - use weapon-specific texture based on who shot
    if (projectile.IsActive()) {
        int si = m_lastShooterIdx;
        bool facingRight = (Directions[si] == 1);
        Texture2D* projTex = nullptr;
        if (si == 0) {
            projTex = &ResourceManager::GetTexture(
                facingRight ? "proj_p1_r" : "proj_p1");
        } else {
            projTex = &ResourceManager::GetTexture(
                facingRight ? "proj_p2_r" : "proj_p2");
        }
        projectile.Draw(*Batch, projTex);
    }

    // Trail particles
    m_trailEmitter->draw(*Batch, ResourceManager::GetTexture("trail"));

    // Explosion effects (bow0-3)
    for (auto& ex : m_explosions) {
        if (!ex.active) continue;
        std::string texName = "explosion_" + std::to_string(ex.frame);
        glm::vec2 drawPos = ex.pos - glm::vec2(40.0f, 40.0f);
        Batch->Draw(ResourceManager::GetTexture(texName),
                    drawPos, glm::vec2(80.0f, 80.0f),
                    0.0f, glm::vec4(1.0f));
    }

    Batch->End();

    // --- 切回屏幕投影（小地图等 HUD） ---
    glm::mat4 screenProjection = glm::ortho(
        0.0f, static_cast<GLfloat>(Width),
        static_cast<GLfloat>(Height), 0.0f, -1.0f, 1.0f);

    // Damage floats (screen-space text)
    for (auto& df : m_damageFloats) {
        if (!df.active) continue;
        glm::vec2 screenPos = m_camera->worldToScreen(df.pos);
        Text->DrawText(df.text, screenPos.x, screenPos.y, 0.7f, df.color);
    }

    Batch->Begin(screenProjection);
    // 小地图
    if (State == GAME_PLAYING || State == GAME_OVER) {
        renderMinimap();
    }
    Batch->End();
}

// ---- ImGui 面板 ----

void Game::RenderImGui() {
    // ESC quit confirmation dialog
    if (m_quitRequested) {
        ImGui::OpenPopup("Quit Game?");
        m_quitRequested = false;
    }
    if (ImGui::BeginPopupModal("Quit Game?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Are you sure you want to quit?");
        if (State == GAME_PLAYING) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                "WARNING: You will lose the current game!");
        }
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(120, 0))) {
            Shutdown();
            glfwSetWindowShouldClose(
                glfwGetCurrentContext(), GL_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    switch (State) {
        case GAME_LOGIN:   RenderMenuPanel(); break;
        case GAME_MENU:    RenderMenuPanel(); break;
        case GAME_WAITING: RenderWaitingPanel(); break;
        case GAME_PLAYING: RenderGameHUD(); break;
        case GAME_OVER:
            RenderGameHUD();
            RenderGameOverPanel();
            break;
    }
    if (State != GAME_LOGIN) RenderChatPanel();
}

void Game::RenderMenuPanel() {
    static char nameBuf[64] = "player";
    static char passBuf[64] = "";
    static char serverBuf[128] = "127.0.0.1:8073";

    if (State == GAME_LOGIN) {
        // 登录/注册界面（居中大面板）
        ImGui::SetNextWindowPos(
            ImVec2(Width / 2.0f - 180.0f, Height / 2.0f - 140.0f),
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
        ImGui::InputText("##server", serverBuf, sizeof(serverBuf));
        ImGui::Spacing();
        ImGui::Text("Player Name:");
        ImGui::InputText("##name", nameBuf, sizeof(nameBuf));
        ImGui::Text("Password:");
        ImGui::InputText("##pass", passBuf, sizeof(passBuf), ImGuiInputTextFlags_Password);
        ImGui::Spacing();

        if (ImGui::Button("Login", ImVec2(100, 30))) {
            ServerAddr = std::string(serverBuf);
            MyName = std::string(nameBuf);
            m_password = std::string(passBuf);
            ddt::NetworkClient::Instance().disconnect();
            StatusText = "CONNECTING...";
            bool ok = ddt::NetworkClient::Instance().connect("ws://" + ServerAddr + "/ddt/game");
            if (!ok) {
                StatusText = "SERVER UNREACHABLE - Check address or try later";
                std::cout << C_RED << "> Connect to " << ServerAddr << " FAILED" << C_RESET << std::endl;
            } else {
                ddt::NetworkClient::Instance().sendLogin(MyName, m_password);
                StatusText = "LOGGING IN...";
                std::cout << C_YELLOW << "> Connect to " << ServerAddr
                          << " as \"" << MyName << "\"" << C_RESET << std::endl;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Register", ImVec2(100, 30))) {
            ServerAddr = std::string(serverBuf);
            MyName = std::string(nameBuf);
            m_password = std::string(passBuf);
            ddt::NetworkClient::Instance().disconnect();
            StatusText = "CONNECTING...";
            bool ok = ddt::NetworkClient::Instance().connect("ws://" + ServerAddr + "/ddt/game");
            if (!ok) {
                StatusText = "SERVER UNREACHABLE - Check address or try later";
            } else {
                ddt::NetworkClient::Instance().sendRegister(MyName, m_password);
                StatusText = "REGISTERING...";
                std::cout << C_YELLOW << "> Register as \"" << MyName << "\"" << C_RESET << std::endl;
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", StatusText.c_str());
        ImGui::End();
    } else {
        // GAME_MENU: 登录后主界面 - 房间列表
        RenderRoomListPanel();
    }
}

void Game::RenderWaitingPanel() {
    RenderRoomLobbyPanel();
}

void Game::RenderGameHUD() {
    ImGuiWindowFlags hudFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus;

    // HP bars - top left
    ImGui::SetNextWindowPos(ImVec2(20, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250, 55), ImGuiCond_Always);
    ImGui::Begin("##hp1", nullptr, hudFlags);
    ImGui::Text("P1 %s", (MyIndex == 0 ? "(You)" : ""));
    ImGui::SameLine();
    char hp1Label[16];
    snprintf(hp1Label, sizeof(hp1Label), "%d/100", HP[0]);
    ImGui::ProgressBar(HP[0] / 100.0f, ImVec2(160, 16), hp1Label);
    ImGui::End();

    // HP bars - top right
    ImGui::SetNextWindowPos(ImVec2(Width - 280, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250, 55), ImGuiCond_Always);
    ImGui::Begin("##hp2", nullptr, hudFlags);
    ImGui::Text("P2 %s", (MyIndex == 1 ? "(You)" : ""));
    ImGui::SameLine();
    char hp2Label[16];
    snprintf(hp2Label, sizeof(hp2Label), "%d/100", HP[1]);
    ImGui::ProgressBar(HP[1] / 100.0f, ImVec2(160, 16), hp2Label);
    ImGui::End();

    // Turn + Wind info - top center
    ImGui::SetNextWindowPos(ImVec2(Width / 2.0f - 100, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, 70), ImGuiCond_Always);
    ImGui::Begin("##info", nullptr, hudFlags);
    if (IsMyTurn) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ">> YOUR TURN <<");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "OPPONENT TURN");
    }
    ImGui::Text("Wind: %d  |  Turn: %d", (int)Wind, (int)TurnNumber);
    ImGui::End();

    // Controls - bottom bar (only when my turn)
    if (IsMyTurn && State == GAME_PLAYING) {
        ImGui::SetNextWindowPos(ImVec2(10, Height - 80), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(Width - 20, 70), ImGuiCond_Always);
        ImGui::Begin("##controls", nullptr, hudFlags);

        // Angle slider
        ImGui::PushItemWidth(180);
        ImGui::SliderInt("Angle", &CurrentAngle, 20, 65);
        ImGui::PopItemWidth();

        ImGui::SameLine();

        // Move buttons
        if (ImGui::Button("< Move")) {
            auto* me = myPlayer();
            if (me->Position.x > 0) {
                float dx = -20.0f;
                me->Position.x += dx;
                if (me->Position.x < 0) me->Position.x = 0;
                ddt::NetworkClient::Instance().sendMove(dx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Move >")) {
            auto* me = myPlayer();
            if (me->Position.x < WORLD_W - me->Size.x) {
                float dx = 20.0f;
                me->Position.x += dx;
                if (me->Position.x > WORLD_W - me->Size.x)
                    me->Position.x = WORLD_W - me->Size.x;
                ddt::NetworkClient::Instance().sendMove(dx);
            }
        }

        ImGui::SameLine();

        // Shoot: two-stage (CHARGE -> FIRE)
        if (!m_imguiCharging) {
            if (ImGui::Button("CHARGE", ImVec2(100, 30))) {
                m_imguiCharging = true;
                IsCharging = true;
                Power = 0.0f;
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("FIRE!", ImVec2(100, 30))) {
                std::cout << C_YELLOW << "> Shoot! angle=" << CurrentAngle
                          << " power=" << (int)Power << C_RESET << std::endl;
                ddt::NetworkClient::Instance().sendShoot(CurrentAngle, Power);
                m_imguiCharging = false;
                IsCharging = false;
                Power = 0.0f;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine();
        ImGui::Text("Power: %d%%", (int)Power);
        ImGui::SameLine();
        ImGui::ProgressBar(Power / 100.0f, ImVec2(120, 16), "");

        ImGui::End();
    }
}

void Game::RenderGameOverPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(Width / 2.0f - 160.0f, Height / 2.0f - 90.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);

    ImGui::Begin("Game Over", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    bool won = (StatusText == "YOU WIN!");
    if (won) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "YOU WIN!");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "YOU LOSE!");
    }

    ImGui::Separator();
    ImGui::Text("Final HP - P1: %d  P2: %d", HP[0], HP[1]);
    ImGui::Spacing();

    if (ImGui::Button("Back to Menu", ImVec2(200, 30))) {
        State = GAME_MENU;
        HP[0] = 100; HP[1] = 100;
        MyIndex = -1;
        IsMyTurn = false;
        IsCharging = false;
        m_imguiCharging = false;
        Power = 0.0f;
        CurrentAngle = 60;
        TurnNumber = 0;
        m_myReady = false;
        CurrentRoomId = 0;
        ddt::NetworkClient::Instance().sendRoomList();
    }

    ImGui::End();
}

void Game::RenderChatPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(Width - 370.0f, Height - 320.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 310), ImGuiCond_FirstUseEver);

    ImGui::Begin("Chat");

    // Channel tabs
    const char* channelNames[] = {"World", "Room", "Team", "All", "Private"};
    int channelIds[] = {3, 2, 0, 1, 6};
    for (int i = 0; i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (m_currentChannel == channelIds[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::SmallButton(channelNames[i])) {
            m_currentChannel = channelIds[i];
        }
        if (active) ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Message display area
    ImGui::BeginChild("##chat_messages", ImVec2(0, -40), true);
    for (auto& cm : m_chatMessages) {
        if (cm.channel != m_currentChannel) continue;
        ImVec4 color;
        switch (cm.channel) {
            case 4: color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); break;  // System = gold
            case 6: color = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;  // Private = purple
            case 0: color = ImVec4(0.0f, 1.0f, 0.5f, 1.0f); break;  // Team = green
            default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break; // White
        }
        std::string line = "[" + cm.sender_name + "] " + cm.message;
        ImGui::TextColored(color, "%s", line.c_str());
    }
    if (m_chatScrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_chatScrollToBottom = false;
    }
    ImGui::EndChild();

    // Input area
    ImGui::Spacing();
    bool enterPressed = ImGui::InputText("##chat_input", m_chatInput, sizeof(m_chatInput),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (enterPressed || ImGui::Button("Send")) {
        if (m_chatInput[0] != '\0') {
            ddt::NetworkClient::Instance().sendChat(m_currentChannel, std::string(m_chatInput));
            memset(m_chatInput, 0, sizeof(m_chatInput));
        }
    }

    ImGui::End();
}

void Game::RenderRoomListPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(Width / 2.0f - 250.0f, 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, Height - 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("DDT - Room List");

    ImGui::Text("Welcome, %s!", MyName.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Logout")) {
        State = GAME_LOGIN;
        m_loggedIn = false;
        ddt::NetworkClient::Instance().disconnect();
    }

    ImGui::Separator();

    // Create room
    static char roomNameBuf[64] = "";
    ImGui::InputText("Room Name", roomNameBuf, sizeof(roomNameBuf));
    ImGui::SameLine();
    if (ImGui::Button("Create Room")) {
        std::string name = roomNameBuf[0] ? std::string(roomNameBuf) : (MyName + "'s Room");
        ddt::NetworkClient::Instance().sendCreateRoom(name);
        std::cout << C_YELLOW << "> Create room: " << name << C_RESET << std::endl;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        ddt::NetworkClient::Instance().sendRoomList();
    }

    ImGui::Separator();

    // Room list
    if (RoomList.empty()) {
        ImGui::TextDisabled("No rooms available. Create one!");
    } else {
        ImGui::BeginChild("##rooms", ImVec2(0, 0), true);
        for (auto& room : RoomList) {
            ImGui::PushID(room.room_id);

            bool isFull = room.player_count >= room.max_players;
            bool inProgress = room.game_started;

            // Room header
            std::string header = "Room #" + std::to_string(room.room_id) +
                " - " + room.room_name +
                " [" + std::to_string(room.player_count) + "/" +
                std::to_string(room.max_players) + "]";

            if (inProgress) header += " [PLAYING]";
            else if (isFull) header += " [FULL]";

            if (ImGui::CollapsingHeader(header.c_str())) {
                ImGui::Indent();

                // Show players with team colors
                for (auto& slot : room.players) {
                    ImVec4 nameColor = (slot.team == 0)
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)   // RED
                        : ImVec4(0.3f, 0.5f, 1.0f, 1.0f);   // BLUE
                    std::string teamTag = (slot.team == 0) ? "[RED] " : "[BLUE] ";
                    std::string readyTag = slot.ready ? " [READY]" : "";
                    ImGui::TextColored(nameColor, "%s%s%s",
                        teamTag.c_str(), slot.player_name.c_str(), readyTag.c_str());
                }

                // Join buttons with team choice
                if (!isFull && !inProgress) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Join RED")) {
                        ddt::NetworkClient::Instance().sendJoinRoom(room.room_id, 0);
                        CurrentRoomId = room.room_id;
                        m_myReady = false;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.3f, 0.7f, 1.0f));
                    if (ImGui::Button("Join BLUE")) {
                        ddt::NetworkClient::Instance().sendJoinRoom(room.room_id, 1);
                        CurrentRoomId = room.room_id;
                        m_myReady = false;
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

void Game::RenderRoomLobbyPanel() {
    ImGui::SetNextWindowPos(
        ImVec2(Width / 2.0f - 250.0f, 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, Height - 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("DDT - Room Lobby");

    ImGui::Text("Room #%d", CurrentRoomId);
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::Button("Leave Room")) {
        ddt::NetworkClient::Instance().sendLeaveRoom();
        CurrentRoomId = 0;
        m_myReady = false;
        State = GAME_MENU;
        ddt::NetworkClient::Instance().sendRoomList();
    }

    ImGui::Separator();

    // Find current room info
    RoomInfoClient* currentRoom = nullptr;
    for (size_t i = 0; i < RoomList.size(); i++) {
        if (RoomList[i].room_id == CurrentRoomId) {
            currentRoom = &RoomList[i];
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

    // RED team
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
        if (slot.player_id == MyId) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Switch BLUE##me")) {
                m_myReady = false;
                ddt::NetworkClient::Instance().sendSwitchTeam(1);
            }
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // BLUE team
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
        if (slot.player_id == MyId) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Switch RED##me")) {
                m_myReady = false;
                ddt::NetworkClient::Instance().sendSwitchTeam(0);
            }
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Ready button
    if (m_myReady) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
        if (ImGui::Button("Cancel Ready", ImVec2(200, 40))) {
            m_myReady = false;
            ddt::NetworkClient::Instance().sendReady(false);
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
        if (ImGui::Button("READY", ImVec2(200, 40))) {
            m_myReady = true;
            ddt::NetworkClient::Instance().sendReady(true);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("All players must be ready. Both teams need at least 1 player.");

    ImGui::End();
}

// ---- 小地图 ----

void Game::renderMinimap() {
    float mmW = 200.0f, mmH = 120.0f;
    float mmX = (float)Width - mmW - 10.0f;
    float mmY = 10.0f;

    // Check minimap click/drag
    if (m_mousePressed) {
        float mx = (float)m_mouseX;
        float my = (float)m_mouseY;
        if (mx >= mmX && mx <= mmX + mmW && my >= mmY && my <= mmY + mmH) {
            // Click in minimap → move camera
            float relX = (mx - mmX) / mmW;
            float relY = (my - mmY) / mmH;
            float worldX = relX * WORLD_W;
            float worldY = relY * WORLD_H;
            m_camera->setCenter(worldX, worldY);
            m_cameraMode = CAM_MANUAL;
            m_manualTimer = 0;
            m_minimapDragging = true;
        }
    }
    if (!m_mousePressed) {
        m_minimapDragging = false;
    }

    // Draw minimap background
    Batch->Draw(ResourceManager::GetTexture("hpbar_bg"),
                glm::vec2(mmX, mmY), glm::vec2(mmW, mmH),
                0.0f, glm::vec4(0.15f, 0.15f, 0.2f, 1.0f));

    // Scale factors: world → minimap
    float sx = mmW / (float)WORLD_W;
    float sy = mmH / (float)WORLD_H;

    // Draw players as dots
    if (Players[0]) {
        float px = mmX + Players[0]->Position.x * sx;
        float py = mmY + Players[0]->Position.y * sy;
        Batch->Draw(ResourceManager::GetTexture("hpbar"),
                    glm::vec2(px - 3, py - 3), glm::vec2(6, 6),
                    0.0f, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));
    }
    if (Players[1]) {
        float px = mmX + Players[1]->Position.x * sx;
        float py = mmY + Players[1]->Position.y * sy;
        Batch->Draw(ResourceManager::GetTexture("hpbar"),
                    glm::vec2(px - 3, py - 3), glm::vec2(6, 6),
                    0.0f, glm::vec4(0.2f, 0.4f, 1.0f, 1.0f));
    }

    // Viewport rectangle
    float vx = mmX + m_camera->getCamX() * sx;
    float vy = mmY + m_camera->getCamY() * sy;
    float vw = m_camera->getViewportW() * sx;
    float vh = m_camera->getViewportH() * sy;

    // Top and bottom edges
    Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy), glm::vec2(vw, 1.5f),
                0.0f, glm::vec4(1.0f));
    Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy + vh - 1.5f), glm::vec2(vw, 1.5f),
                0.0f, glm::vec4(1.0f));
    // Left and right edges
    Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy), glm::vec2(1.5f, vh),
                0.0f, glm::vec4(1.0f));
    Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx + vw - 1.5f, vy), glm::vec2(1.5f, vh),
                0.0f, glm::vec4(1.0f));
}

// ---- 摄像机行为 ----

void Game::updateCamera(float dt) {
    if (!m_camera) return;

    switch (m_cameraMode) {
    case CAM_INTRO: {
        m_introTimer += dt;
        if (m_introPhase == 0) {
            // Show P1 position
            m_camera->panTo(Players[0]->Position.x, Players[0]->Position.y, 600.0f);
            m_introPhase = 1;
            m_introTimer = 0;
        } else if (m_introPhase == 1) {
            if (m_introTimer > 1.5f && !m_camera->isPanning()) {
                m_introPhase = 2;
                m_introTimer = 0;
            }
        } else if (m_introPhase == 2) {
            // Pan to P2
            m_camera->panTo(Players[1]->Position.x, Players[1]->Position.y, 600.0f);
            m_introPhase = 3;
            m_introTimer = 0;
        } else if (m_introPhase == 3) {
            if (m_introTimer > 1.5f && !m_camera->isPanning()) {
                // Done intro → follow turn
                m_cameraMode = CAM_FOLLOW_TURN;
                GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
                m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
            }
        }
        break;
    }

    case CAM_FOLLOW_TURN: {
        // Keep camera centered on current turn player
        GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
        m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 600.0f);
        break;
    }

    case CAM_FOLLOW_PROJ: {
        // Follow projectile
        if (projectile.IsActive()) {
            glm::vec2 pos = projectile.GetCurrentPos();
            m_camera->setCenter(pos.x, pos.y);
        } else {
            // Projectile done → back to turn follow
            m_cameraMode = CAM_FOLLOW_TURN;
            GameObject* turnPlayer = IsMyTurn ? myPlayer() : opponentPlayer();
            m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
        }
        break;
    }

    case CAM_MANUAL: {
        m_manualTimer += dt;
        if (m_manualTimer > 3.0f) {
            // Auto-relock after 3s idle
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
    ddt::NetworkClient::Instance().disconnect();
}