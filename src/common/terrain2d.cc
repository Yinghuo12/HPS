#include "terrain2d.h"

#include <algorithm>
#include <cmath>

namespace ddt {

void Terrain2D::generate(int w, int h, float baseH) {
    m_w = w;
    m_h = h;
    // 每格 1 bit, 行优先
    m_bits.assign((w * h + 7) / 8, 0);

    // 与旧 generateHeightMap 一致的余弦地表公式
    for(int x = 0; x < m_w; ++x) {
        double nx = (double)(x - m_w / 2) / m_w;
        float surfaceH = baseH - (float)(std::cos(nx * m_w * 0.003) * (baseH * 0.45f));
        surfaceH = std::max(baseH * 0.5f, std::min(baseH * 1.6f, surfaceH));
        int top = std::min((int)std::ceil(surfaceH), m_h);
        // 填实 [0, top): 从底部到地表都是实体
        for(int y = 0; y < top; ++y) {
            setSolid(x, y, true);
        }
    }
}

void Terrain2D::setBitmap(const std::string& data, int w, int h) {
    m_bits = data;
    m_w = w;
    m_h = h;
}

bool Terrain2D::isSolid(int x, int y) const {
    if(x < 0 || x >= m_w || y < 0 || y >= m_h) return false;
    return (m_bits[byteIndex(x, y)] >> bitOffset(x, y)) & 1;
}

void Terrain2D::setSolid(int x, int y, bool v) {
    if(x < 0 || x >= m_w || y < 0 || y >= m_h) return;
    int idx = byteIndex(x, y);
    int off = bitOffset(x, y);
    if(v) m_bits[idx] |= (1 << off);
    else  m_bits[idx] &= ~(1 << off);
}

void Terrain2D::removeCircle(float cx, float cy, float r) {
    int minX = std::max(0, (int)(cx - r));
    int maxX = std::min(m_w - 1, (int)(cx + r));
    int minY = std::max(0, (int)(cy - r));
    int maxY = std::min(m_h - 1, (int)(cy + r));
    float r2 = r * r;
    for(int x = minX; x <= maxX; ++x) {
        for(int y = minY; y <= maxY; ++y) {
            float dx = x - cx;
            float dy = y - cy;
            if(dx * dx + dy * dy <= r2) {
                setSolid(x, y, false);
            }
        }
    }
}

float Terrain2D::columnHeight(int x) const {
    if(x < 0 || x >= m_w) return 0;
    // 从顶部往下找第一个实体格
    for(int y = m_h - 1; y >= 0; --y) {
        if(isSolid(x, y)) return (float)(y + 1);   // 格的顶部 = y+1
    }
    return 0;   // 全空
}

} // namespace ddt
