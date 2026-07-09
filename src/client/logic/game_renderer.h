#ifndef GAME_RENDERER_H
#define GAME_RENDERER_H

class Game;

// 从 Game 类中提取的世界渲染模块
// 负责绘制地形、人物、弹道、粒子特效、小地图等
class GameRenderer {
public:
    explicit GameRenderer(Game& game);

    // 渲染游戏世界（地形、人物、弹道、粒子、爆炸、伤害数字、小地图）
    void Render();

private:
    void renderMinimap();

    Game& m_game;
};

#endif
