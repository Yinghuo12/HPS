#include "game_renderer.h"
#include "game.h"
#include "resource_manager.h"
#include "sprite_batch.h"
#include "terrain.h"
#include "text_renderer.h"
#include "common/ddt_physics.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

GameRenderer::GameRenderer(Game& game) : m_game(game) {}

void GameRenderer::Render() {
    if (m_game.State == GAME_LOGIN || m_game.State == GAME_MENU) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    float camX = m_game.m_camera->getCamX();
    float camY = m_game.m_camera->getCamY();
    glm::mat4 gameProjection = glm::ortho(
        camX, camX + m_game.m_camera->getViewportW(),
        camY + m_game.m_camera->getViewportH(), camY,
        -1.0f, 1.0f);
    
    // --- 切回屏幕投影（渲染静态全屏背景） ---
    glm::mat4 screenProjection = glm::ortho(
        0.0f, static_cast<GLfloat>(m_game.Width),
        static_cast<GLfloat>(m_game.Height), 0.0f, -1.0f, 1.0f);
    
    // 天空背景使用 screenProjection 绘制
    m_game.Batch->Begin(screenProjection);
    m_game.Batch->Draw(ResourceManager::GetTexture("sky"),
                glm::vec2(0.0f, 0.0f),
                glm::vec2((float)m_game.Width, (float)m_game.Height));
    m_game.Batch->End();
    
    // 绘制可被物理刨开、挖空的游戏地形层
    m_game.GameTerrain->Draw(camX, camY, m_game.m_camera->getViewportW(), m_game.m_camera->getViewportH());
    
    // 绘制人物、武器、炮弹和粒子特效
    m_game.Batch->Begin(gameProjection);
    
    // 人物渲染（随地形坡度旋转）
    for (int i = 0; i < 2; i++) {
        if (!m_game.Players[i]) continue;
        float slopeAngle = 0.0f;
        if (m_game.GameTerrain) {
            auto& hm = m_game.GameTerrain->GetHeightMap();
            int ix = (int)(m_game.Players[i]->Position.x + m_game.Players[i]->Size.x * 0.5f);
            if (ix >= 3 && ix < (int)hm.size() - 3) {
                float slopeDeg = ddt::PhysicsEngine::getSlopeAngle((float)ix, hm);
                slopeAngle = glm::radians(-slopeDeg);
            }
        }
        auto& tex = (m_game.Directions[i] == 1)
            ? ResourceManager::GetTexture(i == 0 ? "player1_r" : "player2_r")
            : ResourceManager::GetTexture(i == 0 ? "player1" : "player2");
        m_game.Batch->Draw(tex, m_game.Players[i]->Position, m_game.Players[i]->Size, slopeAngle, glm::vec4(1.0f));
    }
    
    // 炮管渲染
    for (int i = 0; i < 2; i++) {
        if (!m_game.Players[i]) continue;
        
        bool facingRight = (m_game.Directions[i] == 1);
        float absAngleDeg;
        
        if (m_game.MyIndex == i) {
            // 本机玩家：自己选择的基础角度（UI角度），需结合本地坡度转为绝对角度渲染
            absAngleDeg = m_game.CurrentAngle;
            if (m_game.GameTerrain) {
                auto& hm = m_game.GameTerrain->GetHeightMap();
                int ix = (int)(m_game.Players[i]->Position.x + m_game.Players[i]->Size.x * 0.5f);
                if (ix >= 1 && ix < (int)hm.size() - 1) {
                    float slopeDeg = ddt::PhysicsEngine::getSlopeAngle((float)ix, hm);
                    absAngleDeg += (facingRight ? slopeDeg : -slopeDeg); // 叠加坡度
                }
            }
        } else {
            // 完美方案核心修正：由于服务端下发的就是计算完毕的地形绝对角度，所以这里不需要再叠加坡度！
            absAngleDeg = m_game.OpponentAngle;
        }
    
        float angleRad = glm::radians(absAngleDeg);
        float bx = facingRight ? m_game.Players[i]->Position.x + 25.0f : m_game.Players[i]->Position.x + 7.0f;
        float by = m_game.Players[i]->Position.y - 10.0f;
        float rot = facingRight ? -angleRad : -(3.14159f - angleRad);
        m_game.Batch->Draw(ResourceManager::GetTexture("barrel"),
                    glm::vec2(bx, by), glm::vec2(8.0f, 30.0f), rot);
    }
    
    // 2. 蓄力瞄准预览点（仅对当前自己有效，因为自己用的是基准角度）
    if (m_game.State == GAME_PLAYING && m_game.IsMyTurn && m_game.Players[m_game.MyIndex]) {
        bool facingRight = (m_game.Directions[m_game.MyIndex] == 1);
        float absAngleDeg = m_game.CurrentAngle;
        
        if (m_game.GameTerrain) {
            auto& hm = m_game.GameTerrain->GetHeightMap();
            int ix = (int)(m_game.Players[m_game.MyIndex]->Position.x + m_game.Players[m_game.MyIndex]->Size.x * 0.5f);
            if (ix >= 1 && ix < (int)hm.size() - 1) {
                float slopeDeg = ddt::PhysicsEngine::getSlopeAngle((float)ix, hm);
                absAngleDeg += (facingRight ? slopeDeg : -slopeDeg); // 叠加坡度
            }
        }
    
        float angleRad = glm::radians(absAngleDeg);
        float sx = facingRight ? m_game.Players[m_game.MyIndex]->Position.x + 25.0f : m_game.Players[m_game.MyIndex]->Position.x + 7.0f;
        float sy = m_game.Players[m_game.MyIndex]->Position.y - 10.0f;
    
        for (int i = 1; i <= 5; ++i) {
            float dist = i * 22.0f;
            float px = sx + (facingRight ? cos(angleRad) * dist : -cos(angleRad) * dist);
            float py = sy - sin(angleRad) * dist;
            m_game.Batch->Draw(ResourceManager::GetTexture("hpbar"),
                        glm::vec2(px - 2, py - 2), glm::vec2(4.0f, 4.0f),
                        0.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.7f - (i * 0.12f)));
        }
    }
    
    // 弹道渲染
    if (m_game.projectile.IsActive()) {
        int si = m_game.m_lastShooterIdx;
        bool facingRight = (m_game.Directions[si] == 1);
        Texture2D* projTex = nullptr;
        
        if (m_game.m_projIsFly) {
            projTex = &ResourceManager::GetTexture(facingRight ? "fly_attack_r" : "fly_attack");
        } else {
            if (si == 0) projTex = &ResourceManager::GetTexture(facingRight ? "proj_p1_r" : "proj_p1");
            else         projTex = &ResourceManager::GetTexture(facingRight ? "proj_p2_r" : "proj_p2");
        }
        m_game.projectile.Draw(*m_game.Batch, projTex);
    }
    
    // 粒子拖尾
    m_game.m_trailEmitter->draw(*m_game.Batch, ResourceManager::GetTexture("trail"));
    
    // 爆炸特效
    for (auto& ex : m_game.m_explosions) {
        if (!ex.active) continue;
        std::string texName = "explosion_" + std::to_string(ex.frame);
        glm::vec2 drawPos = ex.pos - glm::vec2(40.0f, 40.0f);
        m_game.Batch->Draw(ResourceManager::GetTexture(texName),
                    drawPos, glm::vec2(80.0f, 80.0f),
                    0.0f, glm::vec4(1.0f));
    }
    
    m_game.Batch->End();
    
    // 伤害数字（用屏幕坐标）
    for (auto& df : m_game.m_damageFloats) {
        if (!df.active) continue;
        glm::vec2 screenPos = m_game.m_camera->worldToScreen(df.pos);
        m_game.Text->DrawText(df.text, screenPos.x, screenPos.y, 0.7f, df.color);
    }
    
    // 小地图
    m_game.Batch->Begin(screenProjection);
    if (m_game.State == GAME_PLAYING || m_game.State == GAME_OVER) {
        renderMinimap();
    }
    m_game.Batch->End();
}

void GameRenderer::renderMinimap() {
    float mmW = 200.0f, mmH = 120.0f;
    float mmX = (float)m_game.Width - mmW - 10.0f;
    float mmY = 10.0f;

    if (m_game.m_mousePressed) {
        float mx = (float)m_game.m_mouseX;
        float my = (float)m_game.m_mouseY;
        if (mx >= mmX && mx <= mmX + mmW && my >= mmY && my <= mmY + mmH) {
            float relX = (mx - mmX) / mmW;
            float relY = (my - mmY) / mmH;
            float worldX = relX * m_game.WORLD_W;
            float worldY = relY * m_game.WORLD_H;
            m_game.m_camera->setCenter(worldX, worldY);
            m_game.m_cameraMode = CAM_MANUAL;
            m_game.m_manualTimer = 0;
            m_game.m_minimapDragging = true;
        }
    }
    if (!m_game.m_mousePressed) {
        m_game.m_minimapDragging = false;
    }
    
    m_game.Batch->Draw(ResourceManager::GetTexture("hpbar_bg"),
                glm::vec2(mmX, mmY), glm::vec2(mmW, mmH),
                0.0f, glm::vec4(0.15f, 0.15f, 0.2f, 1.0f));
    
    float sx = mmW / (float)m_game.WORLD_W;
    float sy = mmH / (float)m_game.WORLD_H;
    
    if (m_game.Players[0]) {
        float px = mmX + m_game.Players[0]->Position.x * sx;
        float py = mmY + m_game.Players[0]->Position.y * sy;
        m_game.Batch->Draw(ResourceManager::GetTexture("hpbar"),
                    glm::vec2(px - 3, py - 3), glm::vec2(6, 6),
                    0.0f, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));
    }
    if (m_game.Players[1]) {
        float px = mmX + m_game.Players[1]->Position.x * sx;
        float py = mmY + m_game.Players[1]->Position.y * sy;
        m_game.Batch->Draw(ResourceManager::GetTexture("hpbar"),
                    glm::vec2(px - 3, py - 3), glm::vec2(6, 6),
                    0.0f, glm::vec4(0.2f, 0.4f, 1.0f, 1.0f));
    }
    
    float vx = mmX + m_game.m_camera->getCamX() * sx;
    float vy = mmY + m_game.m_camera->getCamY() * sy;
    float vw = m_game.m_camera->getViewportW() * sx;
    float vh = m_game.m_camera->getViewportH() * sy;
    
    m_game.Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy), glm::vec2(vw, 1.5f),
                0.0f, glm::vec4(1.0f));
    m_game.Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy + vh - 1.5f), glm::vec2(vw, 1.5f),
                0.0f, glm::vec4(1.0f));
    m_game.Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx, vy), glm::vec2(1.5f, vh),
                0.0f, glm::vec4(1.0f));
    m_game.Batch->Draw(ResourceManager::GetTexture("powerbar"),
                glm::vec2(vx + vw - 1.5f, vy), glm::vec2(1.5f, vh),
                0.0f, glm::vec4(1.0f));
}