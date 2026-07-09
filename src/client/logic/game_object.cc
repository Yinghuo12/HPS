#include "game_object.h"
#include "sprite_renderer.h"
#include "sprite_batch.h"

GameObject::GameObject()
    : Position(0.0f, 0.0f), Size(1.0f, 1.0f), Velocity(0.0f, 0.0f)
    , Color(1.0f), Rotation(0.0f), IsSolid(GL_FALSE), Destroyed(GL_FALSE) {}

GameObject::GameObject(glm::vec2 pos, glm::vec2 size, Texture2D sprite,
                       glm::vec3 color, glm::vec2 velocity)
    : Position(pos), Size(size), Velocity(velocity)
    , Color(color), Rotation(0.0f), IsSolid(GL_FALSE), Destroyed(GL_FALSE)
    , Sprite(sprite) {}

void GameObject::Draw(SpriteRenderer& renderer) {
    renderer.DrawSprite(Sprite, Position, Size, Rotation, Color);
}

void GameObject::Draw(SpriteBatch& batch) {
    batch.Draw(Sprite, Position, Size, Rotation,
               glm::vec4(Color, 1.0f));
}
