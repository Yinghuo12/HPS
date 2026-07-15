// ProjectilePlayer.cs — 弹道回放: 沿轨迹点时间插值移动一个炮弹, 并绘制拖尾
//
// 由 BattleField 在收到 ShootResultNotify 后启动:
//   1. PhysicsSim.ComputeTrajectory 算出轨迹点
//   2. StartPlay(points) 开始动画
//   3. Update 按 t 在点序列线性插值移动炮弹
//   4. 走过的点用 LineRenderer 画一条橙黄色拖尾(复刻旧 C++ 客户端粒子拖尾观感)
//   5. 走完触发 onComplete(用于爆炸效果)
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net.Battle {
public class ProjectilePlayer : MonoBehaviour {
    private List<PhysicsSim.TrajPoint> points_;
    private float elapsed_;
    private int   lastIdx_ = 0;     // 上次插值命中的下标(轨迹 t 单调递增, 摊还 O(1))
    private float speed_ = 1f;      // 回放速度倍率
    private System.Action onComplete_;
    private bool playing_;

    // 拖尾: 环形缓冲(替代 List + RemoveAt(0), 消除 O(n) 元素移动)
    //       + 零分配上传(消除每帧 ToArray 触发的 GC, 卡顿主因)
    private const float TRAIL_WIDTH = 6f;          // 拖尾粗细(世界单位≈像素)
    private const int   TRAIL_MAX = 120;           // 最多保留的拖尾点, 避免太长
    private LineRenderer trail_;
    private readonly Vector3[] trailRing_ = new Vector3[TRAIL_MAX];   // 环形缓冲槽(写入时烘焙 z=-0.5)
    private int trailWrite_ = 0;        // 下一个写入位置
    private int trailCount_ = 0;        // 当前有效点数(<= TRAIL_MAX)
    // 预分配上传缓冲: 构造时一次性分配, 每帧复用, 既零 GC 又只需一次 SetPositions 调用
    private readonly Vector3[] trailUpload_ = new Vector3[TRAIL_MAX];

    public void StartPlay(List<PhysicsSim.TrajPoint> points, System.Action onComplete, float speed = 1f) {
        points_ = points;
        elapsed_ = 0f;
        lastIdx_ = 0;   // 游标归位, 下次查找从轨迹起点开始
        speed_ = speed;
        onComplete_ = onComplete;
        playing_ = points != null && points.Count > 0;
        gameObject.SetActive(playing_);
        EnsureTrail();
        trailWrite_ = 0;
        trailCount_ = 0;   // 环形缓冲清空
        if (trail_ != null) {
            trail_.positionCount = 0;
            trail_.enabled = true;
        }
        if (playing_ && points_.Count > 0) {
            transform.position = new Vector3(points_[0].x, points_[0].y, 0);
            AppendTrail(transform.position);
        }
    }

    // 设置弹丸贴图(贴图已按朝向选好 _r/无后缀, 不再翻转)
    public void SetSprite(Sprite sprite, int direction) {
        var sr = GetComponent<SpriteRenderer>();
        if (sr != null) {
            sr.sprite = sprite;
            sr.flipX = false;          // 贴图本身已是正确朝向(_r=朝右)
            sr.color = Color.white;    // 不染色, 保留贴图原色
        }
    }

    public bool IsActive => playing_;

    void Update() {
        if (!playing_ || points_ == null || points_.Count == 0) return;
        elapsed_ += Time.deltaTime * speed_;
        // 找当前 t 对应的插值位置。
        // 轨迹点的 t 单调递增, 故从上次命中的下标继续向前找, 摊还 O(1)
        // (旧实现每帧从 0 扫, 高抛弹道 points_.Count 可达 1500)。
        int n = points_.Count;
        int idx = lastIdx_;
        while (idx < n && points_[idx].t < elapsed_) idx++;
        if (idx >= n) {
            // 走完全程
            var last = points_[n - 1];
            transform.position = new Vector3(last.x, last.y, 0);
            AppendTrail(transform.position);
            playing_ = false;
            gameObject.SetActive(false);
            if (trail_ != null) trail_.enabled = false;   // 隐藏拖尾(爆炸特效接管)
            onComplete_?.Invoke();
            return;
        }
        lastIdx_ = idx;   // 缓存: 下一帧从这里继续
        // 在 idx-1 和 idx 之间线性插值
        Vector3 pos;
        if (idx == 0) {
            pos = new Vector3(points_[0].x, points_[0].y, 0);
        } else {
            var a = points_[idx - 1];
            var b = points_[idx];
            float span = b.t - a.t;
            float k = span > 0 ? (elapsed_ - a.t) / span : 0;
            pos = new Vector3(Mathf.Lerp(a.x, b.x, k), Mathf.Lerp(a.y, b.y, k), 0);
        }
        transform.position = pos;
        AppendTrail(pos);
    }

    // 拖尾点写入环形缓冲槽(预烘焙 z=-0.5, 避免每次上传再拷贝转换)
    private void AppendTrail(Vector3 pos) {
        if (trail_ == null) return;
        // 仅在与上一个点有可感知距离时追加, 减少抖动与冗余点
        if (trailCount_ > 0) {
            // 读"最新写入的槽"(writeIdx-1 环绕)做距离判断
            int lastSlot = (trailWrite_ - 1 + TRAIL_MAX) % TRAIL_MAX;
            Vector3 last = trailRing_[lastSlot];
            if ((pos - last).sqrMagnitude < 1f) return;   // <1 像素不追加
        }
        // 写入环形缓冲: 满了就覆盖最旧的槽(逻辑上等价 List.RemoveAt(0), 但 O(1))
        trailRing_[trailWrite_].x = pos.x;
        trailRing_[trailWrite_].y = pos.y;
        trailRing_[trailWrite_].z = -0.5f;   // LineRenderer 拖尾的 z(复刻旧实现)
        trailWrite_ = (trailWrite_ + 1) % TRAIL_MAX;
        if (trailCount_ < TRAIL_MAX) trailCount_++;
        // 上传 LineRenderer: 环形缓冲按写入顺序回读(逻辑 [旧→新])到预分配上传缓冲,
        // 再一次性 SetPositions —— 既零托管堆分配(不每帧 ToArray), 又只有一次 native 调用。
        int read = (trailWrite_ - trailCount_ + TRAIL_MAX) % TRAIL_MAX;
        for (int i = 0; i < trailCount_; i++) {
            trailUpload_[i] = trailRing_[read];
            read = (read + 1) % TRAIL_MAX;
        }
        trail_.positionCount = trailCount_;
        trail_.SetPositions(trailUpload_);
    }

    private void EnsureTrail() {
        if (trail_ != null) return;
        var go = new GameObject("Trail");
        go.transform.SetParent(transform.parent, false);   // 不随炮弹节点移动
        trail_ = go.AddComponent<LineRenderer>();
        trail_.useWorldSpace = true;
        trail_.loop = false;
        trail_.material = new Material(Shader.Find("Sprites/Default"));
        // 橙黄色渐变(复刻旧客户端 vec4(1.0,0.8,0.2,0.7) 拖尾观感)
        trail_.startColor = new Color(1f, 0.85f, 0.3f, 0.95f);
        trail_.endColor = new Color(1f, 0.5f, 0.1f, 0.1f);
        trail_.startWidth = TRAIL_WIDTH;
        trail_.endWidth = TRAIL_WIDTH * 0.3f;
        trail_.sortingOrder = 11;   // 在炮弹(sortingOrder=10)之上
        trail_.enabled = false;
    }
}
}
