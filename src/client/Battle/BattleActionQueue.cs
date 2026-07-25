// BattleActionQueue.cs — 串行动作队列: 战斗表现的时序核心
//
// 替代旧的 resolvingShoot_ / camFrozenAfterShoot_ / pendingTurnStart_ + 多个协调协程。
// 所有网络消息(ShootResult/TurnStart/Move/GameOver)到达后转换为 BattleAction 入队,
// 队列按序执行, 每个 Action 自己控制"何时算完成"(IsDone 轮询式)。
//
// 时序保证:
//   - ShootAction 必须在 TurnStartAction 之前完成 → 天然有序(队列串行)
//   - 弹道落地 → 爆炸 → 停顿 → 切回合 → 由 Action 的 IsDone 串联, 无竞态
//   - 队列非 idle 期间相机跟随让位 → 无需 camFrozenAfterShoot_ 等冻结标志
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using Ddt;
using static Ddt.Net.Game.DebugLog;

namespace Ddt.Net.Battle {

// ---- 动作基类 ----
// IsDone 为轮询式(每帧由队列 Update 检查), 非回调式 → 不存在协程忘记启动的问题。
public abstract class BattleAction {
    public bool started { get; private set; }   // Execute 是否已调过
    public abstract bool IsDone { get; }         // 队列据此推进到下一个
    public virtual void Execute() { started = true; }
    public virtual void OnComplete() {}          // IsDone 变 true 后队列调一次(收尾: 隐藏提示等)
    public virtual void OnCancel() {}            // GameOver/销毁 中途取消时的清理
    public virtual string Name => GetType().Name;
}

// ---- 射击结算动作 ----
// PlayTrajectory 弹道回放 → 落地回调(爆炸+伤害+KeepBusy) → 等 IsBusy 清零。
// 替代旧的 OnShootResult + resolvingShoot_ + DelayedApplyTurnStart 的弹道等待部分。
public class ShootAction : BattleAction {
    private readonly BattleField field_;
    private readonly ShootResultNotify data_;
    private readonly PhysicsSim.PhysicsParams physics_;
    private bool landed_;          // 弹道 onComplete 是否已触发
    private float executeRealtime_;   // Execute 调用时的实时时间(不受暂停影响)
    // 总超时: 弹道最长飞行 + 爆炸 + 停顿。超过则强制结算。
    // 用 realtimeSinceStartup: 最小化/失焦时 Time.time 暂停, 但实时时钟继续走。
    private const float TIMEOUT = 8f;

    public ShootAction(BattleField field, ShootResultNotify data, PhysicsSim.PhysicsParams physics) {
        field_ = field; data_ = data; physics_ = physics;
    }

    public override void Execute() {
        base.Execute();
        executeRealtime_ = Time.realtimeSinceStartup;
        // 炮弹刚落地时的视觉结算(爆炸/伤害数字/HP 更新/停顿)
        bool settled = false;   // 防 onLand 被调两次(PlayTrajectory 同步 + 超时)
        System.Action onLand = () => {
            if (settled) return; settled = true;
            landed_ = true;
            bool offscreen = (data_.HitX < 0 || data_.HitX > BattleField.WORLD_W);
            Debug.Log($"[Battle] ShootAction.onLand hit=({data_.HitX},{data_.HitY}) isFly={data_.IsFly} offscreen={offscreen}");
            if (field_ == null) return;
            // 更新所有玩家状态(HP/位置, 含纸飞机传送的落点)
            foreach (var ps in data_.UpdatedPlayers) {
                field_.SetPlayerState(ps.AccountId, ps.X, ps.Y, ps.Hp, ps.MaxHp, ps.Angle, ps.Direction);
            }
            // 普通弹: 落点爆炸(挖坑 + 动画); 纸飞机/出界不爆炸
            if (!data_.IsFly && !offscreen) field_.Explode(data_.HitX, data_.HitY, 50f);
            // 命中玩家: 弹出伤害数字
            if (data_.HitPlayer && data_.Damage > 0) {
                bool crit = data_.DamageType == ShootResultNotify.Types.DamageType.Critical;
                bool block = data_.DamageType == ShootResultNotify.Types.DamageType.Block;
                field_.ShowDamageText(data_.HitX, data_.HitY, data_.Damage, crit, block);
            }
            // 落地后停顿(看清爆炸/命中结果)
            field_.KeepBusyFor(0.8f);
        };

        if (field_ != null && field_.MyPlayer != null) {
            // PlayTrajectory 同步激活炮弹(IsBusy=true), 之后 onLand 在弹道走完时触发。
            // 传入服务端权威落点(serverHitX/Y): 高角度时 (int) 截断导致角度微小差异,
            // 最后一个轨迹点强制对齐到服务端落点, 保证视觉与实际一致。
            field_.PlayTrajectory(data_.StartX, data_.StartY, data_.Angle, data_.Direction,
                data_.Force, data_.Wind, physics_, data_.WeaponId, data_.IsFly, onLand,
                data_.HitX, data_.HitY);
        } else {
            // field 未就绪: 跳过弹道视觉, 直接结算
            onLand();
        }
    }

    // 完成 = 弹道已落地 + 战场不忙碌, 或超时强制完成
    public override bool IsDone {
        get {
            if (!started) return false;
            // 超时: onComplete 未触发(App Nap/Update 停止)或 IsBusy 卡住 → 强制结算
            if (!landed_ && Time.realtimeSinceStartup - executeRealtime_ >= TIMEOUT) {
                Debug.LogWarning($"[Battle] ShootAction TIMEOUT ({TIMEOUT}s), force settle");
                // 强制调 onLand 的结算逻辑(通过设 landed_ + 清炮弹)
                if (field_ != null && field_.IsProjectileActive) field_.StopProjectile();
                landed_ = true;
                // 直接应用结算(不播爆炸, 只更新状态)
                if (field_ != null) {
                    foreach (var ps in data_.UpdatedPlayers) {
                        field_.SetPlayerState(ps.AccountId, ps.X, ps.Y, ps.Hp, ps.MaxHp, ps.Angle, ps.Direction);
                    }
                }
                return true;
            }
            return landed_ && (field_ == null || !field_.IsBusy);
        }
    }

    public override void OnCancel() {
        // GameOver 中途: 强制清除残留炮弹, 防 IsBusy 永真
        if (field_ != null && field_.IsProjectileActive) field_.StopProjectile();
    }
}

// ---- 回合切换动作 ----
// 立即应用回合状态(turnAccountId/wind/timeLeft/解锁操作), 延迟刷新玩家位置(等 IsBusy),
// 显示提示 + 平移相机, 提示淡出后完成。
// 替代旧的 ApplyTurnStart + DelayedTurnStartPlayerUpdate + ShowTurnPromptAndPan。
public class TurnStartAction : BattleAction {
    private readonly BattleController ctrl_;
    private readonly TurnStartNotify data_;

    public TurnStartAction(BattleController ctrl, TurnStartNotify data) {
        ctrl_ = ctrl; data_ = data;
    }

    public override void Execute() {
        base.Execute();
        ctrl_.ApplyTurnState(data_);
        // 不在这里调 PanToCamera:
        //   - 首回合: PlayIntro 收尾已平移到本回合玩家, PanToCamera 会干扰 intro 相机序列
        //     (覆盖 camTarget_ 导致 WaitUntilCamReachedOrTimeout 误判 → intro 卡顿)
        //   - 后续回合: 队列空闲后 Update 的 FocusCamera 会平滑移到新回合玩家
        //   - 提示显示用独立协程, 不阻塞队列(旧实现等 1.4s 会延迟后续 ShootAction)
        ctrl_.ShowTurnPromptAndFade(ctrl_.MyTurn);
    }

    // 立即完成: 回合状态切换是瞬时的, 提示淡出不应阻塞后续动作
    public override bool IsDone => started;
}

// ---- 游戏结束动作 ----
// 清空队列 + 显示结算面板。Execute 后队列不再处理任何后续 Action。
public class GameOverAction : BattleAction {
    private readonly BattleController ctrl_;
    private readonly GameOverNotify data_;
    public GameOverAction(BattleController ctrl, GameOverNotify data) { ctrl_ = ctrl; data_ = data; }

    public override void Execute() {
        base.Execute();
        ctrl_.OnGameOver(data_);
    }

    public override bool IsDone => started;
}

// ---- 纯延迟动作(看清爆炸/命中结果后切回合) ----
public class WaitAction : BattleAction {
    private readonly float duration_;
    private float startTime_ = -1f;
    public WaitAction(float duration) { duration_ = duration; }

    public override void Execute() {
        base.Execute();
        startTime_ = Time.time;
    }

    public override bool IsDone => started && startTime_ > 0f && Time.time - startTime_ >= duration_;
}

// ---- 串行动作队列调度器 ----
// 挂在 BattleController 的 GameObject 上(MonoBehaviour 生命周期驱动 Update)。
public class BattleActionQueue : MonoBehaviour {
    private readonly Queue<BattleAction> queue_ = new Queue<BattleAction>();
    private BattleAction current_;
    private bool gameOver_;

    // 入队(GameOver 后忽略新动作)
    public void Enqueue(BattleAction action) {
        if (gameOver_) return;
        queue_.Enqueue(action);
        DBGLogT("Battle", $"ActionQueue Enqueue: {action.Name} (pending={queue_.Count})");
    }

    // 队列是否空闲(无正在执行的动作)。相机跟随在 idle 时才恢复。
    public bool IsIdle => current_ == null && queue_.Count == 0;

    // 清空队列(GameOver/退出战斗时调)
    public void Clear() {
        if (current_ != null) current_.OnCancel();
        current_ = null;
        queue_.Clear();
    }

    public void MarkGameOver() {
        gameOver_ = true;
        Clear();
    }

    void Update() {
        if (gameOver_) return;   // GameOver 后不再处理任何动作
        // 无 current 时出队一个并 Execute
        if (current_ == null) {
            if (queue_.Count == 0) return;
            current_ = queue_.Dequeue();
            DBGLogT("Battle", $"ActionQueue Execute: {current_.Name}");
            current_.Execute();
            // GameOverAction 执行后: 标记 gameOver, 清空队列(含正在执行的), 不再处理后续
            if (current_ is GameOverAction) {
                gameOver_ = true;
                current_ = null;
                queue_.Clear();
                return;
            }
        }
        // current 有且 IsDone → 完成收尾, 出队下一个
        if (current_ != null && current_.IsDone) {
            DBGLogT("Battle", $"ActionQueue Done: {current_.Name}");
            current_.OnComplete();
            current_ = null;
        }
    }
}

} // namespace Ddt.Net.Battle
