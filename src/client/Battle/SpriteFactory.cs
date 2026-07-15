// SpriteFactory.cs — 程序生成纯色 2D Sprite
// 统一采用 pixelsPerUnit = 1f，确保 1 像素对齐 1 世界单位，与地形比例 1:1 完美契合
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net.Battle {
public static class SpriteFactory {
    private static readonly Dictionary<int, Sprite> cache_ = new Dictionary<int, Sprite>();

    /// <summary>生成一个纯色矩形 Sprite</summary>
    public static Sprite MakeRect(int w, int h, Color color) {
        int key = Hash(w, h, color);
        Sprite sp;
        if (cache_.TryGetValue(key, out sp)) return sp;
        var tex = new Texture2D(w, h) { filterMode = FilterMode.Point };
        Color[] px = new Color[w * h];
        for (int i = 0; i < px.Length; i++) px[i] = color;
        tex.SetPixels(px);
        tex.Apply();
        // 关键改动：将 100f 改为 1f
        sp = Sprite.Create(tex, new Rect(0, 0, w, h), new Vector2(0.5f, 0.5f), 1f);
        cache_[key] = sp;
        return sp;
    }

    /// <summary>生成一个纯色圆形 Sprite</summary>
    public static Sprite MakeCircle(int diameter, Color color) {
        int key = -(Hash(diameter, diameter, color) ^ 0x54321);
        Sprite sp;
        if (cache_.TryGetValue(key, out sp)) return sp;
        var tex = new Texture2D(diameter, diameter) { filterMode = FilterMode.Point };
        Color[] px = new Color[diameter * diameter];
        float c = diameter / 2f;
        float r2 = c * c;
        for (int y = 0; y < diameter; y++) {
            for (int x = 0; x < diameter; x++) {
                float dx = x + 0.5f - c, dy = y + 0.5f - c;
                px[y * diameter + x] = (dx * dx + dy * dy <= r2) ? color : new Color(0, 0, 0, 0);
            }
        }
        tex.SetPixels(px);
        tex.Apply();
        // 关键改动：将 100f 改为 1f
        sp = Sprite.Create(tex, new Rect(0, 0, diameter, diameter), new Vector2(0.5f, 0.5f), 1f);
        cache_[key] = sp;
        return sp;
    }

    private static int Hash(int w, int h, Color c) {
        return (w * 73856093) ^ (h * 19349663)
             ^ (int)(c.r * 255) << 16 ^ (int)(c.g * 255) << 8 ^ (int)(c.b * 255);
    }
}
}