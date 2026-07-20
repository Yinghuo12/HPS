#ifndef __DDT_TERRAIN2D_H__
#define __DDT_TERRAIN2D_H__

#include <cstdint>
#include <string>

namespace ddt {

// Terrain2D — 二维体素地形(bitset 网格)
// 每 bit = 1 格(1×1 世界单位)。1=有地形实体, 0=空。
// 内存: w*h/8 字节(3000×420/8 ≈ 158KB/房间)
class Terrain2D {
public:
    Terrain2D() : m_w(0), m_h(0) {}

    // 生成初始地形: 余弦起伏的地表, 与旧 generateHeightMap 公式一致
    // w: X 维度(3000), h: Y 维度(420), baseH: 地面基础高度
    void generate(int w, int h, float baseH);

    // 序列化/反序列化(用于 proto 传输)
    const std::string& bitmap() const { return m_bits; }
    void setBitmap(const std::string& data, int w, int h);

    // 查某格是否实体。越界返回 false(天空/地图外 = 空)
    bool isSolid(int x, int y) const;

    // 设某格
    void setSolid(int x, int y, bool v);

    // 爆炸挖圆: 把圆心 (cx, cy)、半径 r 内的格子全部清除
    // 只影响圆内——圆外的平台/地形完整保留(解决旧 1D 模型整列降低的问题)
    void removeCircle(float cx, float cy, float r);

    // 该列最高实体格的 y(等价旧 heightMap[x])
    // 全空返回 0。用于玩家站位/坡度计算
    float columnHeight(int x) const;

    int width() const { return m_w; }
    int height() const { return m_h; }
    bool empty() const { return m_bits.empty(); }

private:
    int m_w, m_h;
    std::string m_bits;   // bitset(每 byte 8 格), 行优先: bit[x + y*m_w]

    inline int byteIndex(int x, int y) const { return (x + y * m_w) >> 3; }
    inline int bitOffset(int x, int y) const { return (x + y * m_w) & 7; }
};

} // namespace ddt

#endif
