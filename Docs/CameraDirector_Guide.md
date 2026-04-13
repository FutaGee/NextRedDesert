# Camera Director 使用指南

本文档说明如何在项目中配置和使用运行时 `Camera Director` 框架。

该系统的目标是提供一个轻量、可扩展的第三人称动作游戏相机框架：

- 使用数据资产配置长期基础相机状态，例如 `Idle`、`Walk`、`Run`、`Jump`。
- 使用可播放的相机逻辑资产处理临时特殊相机，例如锁定、受击 FOV Kick、处决、Boss 入场等。
- 后续新增特殊相机规则时，优先新增 `UCameraLogicAsset` 子类，不需要修改核心状态机。

## 1. 核心文件位置

源码位置：

```text
Source/AITestProject/CameraDirector/
Source/AITestProject/CameraDirector/Assets/
Source/AITestProject/CameraDirector/Runtime/
Source/AITestProject/CameraDirector/Types/
```

主要类：

```text
UCameraDirectorComponent
UCameraStateMachineAsset
UCameraLogicAsset
ULockOnCameraLogic
UAdditiveFOVKickCameraLogic
```

## 2. 推荐接入方式

### 2.1 给玩家角色添加组件

在玩家角色蓝图中添加组件：

```text
Camera Director Component
```

推荐配置：

```text
Target Spring Arm = 角色上的 CameraBoom / SpringArm
Target Camera = 角色上的 FollowCamera / CameraComponent
State Machine Asset = 你的基础相机状态机数据资产
```

如果 `Target Spring Arm` 或 `Target Camera` 没有手动指定，组件会在 `BeginPlay` 时尝试从 Owner 上自动查找第一个 `USpringArmComponent` 和 `UCameraComponent`。

建议仍然手动指定，避免角色上有多个相机组件时匹配到错误目标。

### 2.2 当前相机应用方式

当前 v1 优先使用 `SpringArm + CameraComponent` 工作流：

```text
SpringArm.TargetArmLength
SpringArm.SocketOffset
SpringArm.TargetOffset
SpringArm.CameraLagSpeed
SpringArm.CameraRotationLagSpeed
SpringArm.bUsePawnControlRotation
Camera.FieldOfView
Controller.ControlRotation
```

如果没有 `SpringArm`，系统会直接设置 `CameraComponent` 的世界位置和旋转。

注意：当使用 `SpringArm` 时，`FCameraResult.CameraLocation` 会被计算和保存，但不会强行覆盖最终相机世界坐标。实际位置仍由 `SpringArm` 处理碰撞、臂长和偏移。

## 3. 创建基础相机状态机资产

在 Content Browser 中创建一个继承自 `UCameraStateMachineAsset` 的 Data Asset。

建议命名：

```text
DA_CameraStateMachine_Player
```

### 3.1 State Machine 顶层参数

| 参数 | 含义 |
| --- | --- |
| `EntryState` | 初始状态名。建议填 `Idle`。 |
| `States` | 基础相机状态列表，例如 `Idle`、`Walk`、`Run`、`Jump`。 |
| `Transitions` | 状态切换规则数组。 |

### 3.2 States 配置建议

每个 `FCameraStateDefinition` 包含：

| 参数 | 含义 |
| --- | --- |
| `StateName` | 状态名。需要和 Transition 中的 `FromState` / `ToState` 对应。 |
| `Params` | 该状态下的相机基础参数。 |
| `Priority` | 预留字段。当前 v1 状态选择主要由 Transition 控制。 |

建议先配置 4 个状态：

```text
Idle
Walk
Run
Jump
```

推荐初始参数示例：

| 状态 | TargetArmLength | FOV | TargetOffset | SocketOffset | 用途 |
| --- | ---: | ---: | --- | --- | --- |
| `Idle` | `360` | `65` | `(0,0,65)` | `(0,35,20)` | 站立待机，较稳定 |
| `Walk` | `380` | `67` | `(0,0,65)` | `(0,40,25)` | 普通移动，略有空间 |
| `Run` | `430` | `72` | `(0,0,70)` | `(0,45,30)` | 奔跑，拉远并增大 FOV |
| `Jump` | `450` | `70` | `(0,0,80)` | `(0,35,35)` | 跳跃/下落，看清落点 |

这些数值只是起点，应根据角色体型、移动速度、关卡尺度调整。

### 3.3 FCameraBasicParams 参数说明

| 参数 | 含义 | 调整建议 |
| --- | --- | --- |
| `TargetArmLength` | SpringArm 臂长。值越大相机越远。 | 奔跑、跳跃可略大；室内场景不宜过大。 |
| `SocketOffset` | 相机在 SpringArm 末端的偏移。 | `Y` 可做越肩偏移，`Z` 可抬高视角。 |
| `TargetOffset` | SpringArm 目标点偏移。 | 通常用 `Z` 把相机关注点抬到角色胸口或头部附近。 |
| `FOV` | Camera Field Of View。 | 高速状态可提高，过高会产生速度感但可能变形。 |
| `CameraLagSpeed` | 相机位置滞后速度。 | 值越大越快跟随；值太低会拖沓。 |
| `CameraRotationLagSpeed` | 相机旋转滞后速度。 | 锁定或战斗中可更高，探索可稍低。 |
| `bUsePawnControlRotation` | 是否跟随 Pawn/Controller 控制旋转。 | 第三人称常用 `true`。 |
| `YawOffset` | 基础 yaw 偏移。 | 可做固定侧向视角微调。 |
| `PitchOffset` | 基础 pitch 偏移。 | 可微调俯仰角，避免角色挡住画面。 |

## 4. 配置状态切换 Transitions

每个 `FCameraTransitionDefinition` 包含：

| 参数 | 含义 |
| --- | --- |
| `FromState` | 当前状态。 |
| `ToState` | 目标状态。 |
| `Rule` | 切换规则。 |
| `BlendSettings` | 切换混合设置。 |
| `bCanInterrupt` | 如果当前正在 Blend，该 Transition 是否允许打断当前 Blend。 |
| `Priority` | 多个 Transition 同时满足时，选择优先级最高的。 |

### 4.1 推荐 Transition 配置

| FromState | ToState | RuleType | 建议参数 | Priority |
| --- | --- | --- | --- | ---: |
| `Idle` | `Walk` | `SpeedGreaterThan` | `Threshold = 10` | `0` |
| `Walk` | `Idle` | `SpeedLessThan` | `Threshold = 5` | `0` |
| `Walk` | `Run` | `BoolFlag` | `FlagName = IsSprinting`, `bExpectedValue = true` | `10` |
| `Run` | `Walk` | `BoolFlag` | `FlagName = IsSprinting`, `bExpectedValue = false` | `10` |
| `Idle` | `Jump` | `IsInAir` | 无 | `100` |
| `Walk` | `Jump` | `IsInAir` | 无 | `100` |
| `Run` | `Jump` | `IsInAir` | 无 | `100` |
| `Jump` | `Idle` | `IsGrounded` | 可配低优先级 | `0` |
| `Jump` | `Walk` | `SpeedGreaterThan` | `Threshold = 10` | `1` |

如果想从 `Jump` 回到 `Walk` 时确保已经落地，当前 v1 单个 Transition 只支持一个 Rule。简单做法是：

```text
Jump -> Idle: IsGrounded, Priority 0
Jump -> Walk: SpeedGreaterThan, Priority 1
```

更严格的复合条件可在后续版本扩展，或由外部逻辑在落地后调用 `ForceSetBaseState`。

### 4.2 Transition Rule 类型说明

| RuleType | 含义 |
| --- | --- |
| `AlwaysTrue` | 永远满足。谨慎使用，容易导致状态立即跳转。 |
| `SpeedGreaterThan` | `Context.Speed > Threshold` 时满足。 |
| `SpeedLessThan` | `Context.Speed < Threshold` 时满足。 |
| `IsInAir` | CharacterMovement 处于 Falling 时满足。 |
| `IsGrounded` | 不在空中时满足。 |
| `WantsLockOn` | `CameraDirectorComponent.SetCameraFlags` 传入的 `bWantsLockOn` 为 true。 |
| `HasLockTarget` | `SetLockTarget` 设置了有效目标。 |
| `BoolFlag` | 按 `FlagName` 查询一个内置布尔上下文字段。 |

当前 `BoolFlag` 支持的 `FlagName`：

```text
IsSprinting
IsAttacking
IsHitReact
IsAccelerating
IsInAir
WantsLockOn
HasLockTarget
```

## 5. Blend 参数说明

`FCameraBlendSettings` 包含：

| 参数 | 含义 |
| --- | --- |
| `BlendTime` | 混合时长，单位秒。 |
| `BlendFunction` | 混合曲线类型。 |
| `BlendExponent` | Ease 曲线指数。值越大曲线越明显。 |

`BlendFunction` 可选：

```text
Linear
EaseIn
EaseOut
EaseInOut
```

推荐：

```text
普通 Idle/Walk/Run: BlendTime 0.15 - 0.3, EaseInOut
进入 Jump: BlendTime 0.1 - 0.2, EaseOut
特殊相机 Override: BlendIn 0.15 - 0.35, BlendOut 0.15 - 0.4
受击 FOV Kick: BlendIn 0.02 - 0.06, BlendOut 0.1 - 0.2
```

## 6. 使用特殊相机逻辑资产

特殊相机逻辑基类是 `UCameraLogicAsset`。

它类似一个轻量版“相机 Montage”：

```text
PlayCameraLogic
StopCameraLogicBySlot
StopAllCameraLogic
Priority
BlendIn
BlendOut
Override / Additive
```

### 6.1 UCameraLogicAsset 通用参数

| 参数 | 含义 |
| --- | --- |
| `LogicName` | 逻辑名，方便识别。 |
| `ApplyMode` | 应用方式：`Override` 或 `Additive`。 |
| `SlotName` | 插槽名。同插槽 Additive 会替换旧实例。 |
| `Priority` | 优先级。Override 逻辑会根据优先级和中断规则处理替换。 |
| `BlendIn` | 播放进入时的混合设置。 |
| `BlendOut` | 停止时的混合设置。 |
| `bAutoFinish` | 是否允许逻辑自动结束。 |

### 6.2 ApplyMode 说明

| ApplyMode | 含义 | 适合用途 |
| --- | --- | --- |
| `Override` | 先基于基础相机生成一个覆盖结果，再按 BlendAlpha 混入。v1 同时只允许一个 Override。 | 锁定、处决、Boss 入场、脚本镜头 |
| `Additive` | 在当前结果上做增量修改。v1 支持多个 Additive。 | FOV Kick、轻微 Shake、闪避强调、受击偏移 |

## 7. 配置 Lock-on Camera Logic

创建一个 `ULockOnCameraLogic` 类型资产，建议命名：

```text
DA_CameraLogic_LockOn
```

默认配置：

```text
ApplyMode = Override
SlotName = LockOn
Priority = 100
BlendIn.BlendTime = 0.2
BlendOut.BlendTime = 0.18
```

### 7.1 Lock-on 参数说明

| 参数 | 含义 | 调整建议 |
| --- | --- | --- |
| `MidpointBias` | 玩家和目标之间的关注点偏向。`0` 偏玩家，`1` 偏目标。 | 通常 `0.4 - 0.6`。 |
| `MinDistance` | 目标很近时的相机距离。 | 太小会贴脸，太大近战不紧凑。 |
| `MaxDistance` | 目标较远时的相机距离。 | 远距离战斗可增大。 |
| `MinFOV` | 目标近时 FOV。 | 近战可略低，画面更稳定。 |
| `MaxFOV` | 目标远时 FOV。 | 远距离可略高，保证双方入镜。 |
| `HeightOffset` | 关注点高度偏移。 | 角色越高，值应越大。 |
| `LateralOffset` | 侧向偏移。 | 用于越肩构图，正负方向可根据项目需求调整。 |
| `RecenterSpeed` | 预留的回中速度参数。当前 v1 暂未深度使用。 |
| `RotationInterpSpeed` | 锁定旋转插值速度。 | 值越大越快对准目标。 |
| `PositionInterpSpeed` | 锁定位置/关注点插值速度。 | 值越大越贴合目标变化。 |
| `bFinishWhenTargetIsLost` | 丢失目标时是否自动结束。 | 一般保持 true。 |

### 7.2 蓝图触发 Lock-on

在角色蓝图或 C++ 中：

```text
CameraDirector->SetLockTarget(TargetActor)
CameraDirector->SetCameraFlags(false, false, false, true)
CameraDirector->PlayCameraLogic(DA_CameraLogic_LockOn)
```

退出锁定：

```text
CameraDirector->SetLockTarget(None)
CameraDirector->SetCameraFlags(false, false, false, false)
CameraDirector->StopCameraLogicBySlot("LockOn")
```

如果 `bFinishWhenTargetIsLost = true`，目标为空时 Lock-on logic 也会自动进入 BlendOut。

## 8. 配置 Additive FOV Kick

创建一个 `UAdditiveFOVKickCameraLogic` 类型资产，建议命名：

```text
DA_CameraLogic_FOVKick_Hit
DA_CameraLogic_FOVKick_Dodge
```

默认配置：

```text
ApplyMode = Additive
SlotName = Reaction
Priority = 10
bAutoFinish = true
BlendIn.BlendTime = 0.03
BlendOut.BlendTime = 0.12
```

### 8.1 FOV Kick 参数说明

| 参数 | 含义 | 调整建议 |
| --- | --- | --- |
| `FOVKickAmount` | 额外增加的 FOV。 | 受击可 `4 - 8`，闪避可 `6 - 12`。 |
| `Duration` | 效果持续时间。 | 通常 `0.15 - 0.4` 秒。 |
| `IntensityCurve` | 可选强度曲线。横轴建议 `0 - 1`。 | 不填时线性衰减：从 1 到 0。 |

### 8.2 蓝图触发 FOV Kick

```text
CameraDirector->PlayCameraLogic(DA_CameraLogic_FOVKick_Hit)
```

由于它是 Additive 且会自动结束，通常不需要手动 Stop。

如果同一 `SlotName` 重复播放，新实例会请求停止旧实例，避免同类效果无限叠加。

## 9. 创建自定义特殊相机逻辑

推荐方式：

1. 新建 Blueprint Class。
2. 父类选择 `CameraLogicAsset` 或某个 C++ 子类。
3. 设置 `ApplyMode`、`SlotName`、`Priority`、`BlendIn`、`BlendOut`。
4. 按需覆写事件：

```text
OnActivated
TickLogic
EvaluateCamera
IsFinished
CanBeInterruptedBy
OnDeactivated
```

### 9.1 事件含义

| 事件 | 用途 |
| --- | --- |
| `OnActivated` | 播放开始时调用，适合初始化缓存值。 |
| `TickLogic` | 每帧更新运行时状态，适合计时、检测目标、触发结束。 |
| `EvaluateCamera` | 修改相机结果。Override 通常生成目标相机结果；Additive 通常只改 FOV/Offset 等增量。 |
| `IsFinished` | 返回是否应该结束。 |
| `CanBeInterruptedBy` | 当前逻辑是否允许被另一个逻辑打断。 |
| `OnDeactivated` | BlendOut 完成、实例移除前调用。 |

### 9.2 自定义逻辑建议

Override 逻辑建议：

```text
适合强控制镜头。
例如处决、Boss 入场、对话镜头、锁定构图。
不要频繁播放低优先级 Override，避免抢占基础相机。
```

Additive 逻辑建议：

```text
适合短暂强调。
例如受击 FOV Kick、闪避推进、轻微偏移、短促冲击。
尽量设置 Duration 和 bAutoFinish，避免遗留激活实例。
```

## 10. C++ 使用示例

示例：

```cpp
if (CameraDirector)
{
	CameraDirector->SetLockTarget(TargetActor);
	CameraDirector->SetCameraFlags(false, false, false, TargetActor != nullptr);
	CameraDirector->PlayCameraLogic(LockOnLogicAsset);
}
```

停止：

```cpp
if (CameraDirector)
{
	CameraDirector->StopCameraLogicBySlot(TEXT("LockOn"));
	CameraDirector->SetLockTarget(nullptr);
}
```

强制基础状态：

```cpp
CameraDirector->ForceSetBaseState(TEXT("Run"));
```

## 11. 与现有角色逻辑的关系

当前实现没有直接修改现有角色类。

如果角色已有自己的相机系统，例如 `ADMCameraCharacter` 内部已经在 Tick 中更新相机，请避免两个系统同时驱动同一个 `SpringArm` 和 `CameraComponent`。

推荐二选一：

```text
方案 A：临时禁用旧相机 Tick 更新，只使用 CameraDirector。
方案 B：CameraDirector 先挂到测试角色/新角色上验证，不接管已有角色。
```

如果两个系统同时写：

```text
SpringArm.TargetArmLength
SpringArm.SocketOffset
Camera.FieldOfView
Controller.ControlRotation
```

最终效果会取决于 Tick 顺序，可能出现抖动或参数被覆盖。

## 12. 调试建议

常见问题：

| 现象 | 检查项 |
| --- | --- |
| 相机没有变化 | 确认 `CameraDirectorComponent` 已添加，`StateMachineAsset` 已指定。 |
| FOV 没变化 | 确认 `TargetCamera` 正确指向实际使用的相机组件。 |
| 锁定无效果 | 确认先调用了 `SetLockTarget(TargetActor)`，目标不为空。 |
| 状态不切换 | 确认 `EntryState` 和 `StateName` 拼写一致，Transition 的 `FromState` / `ToState` 有效。 |
| 奔跑状态不进入 | 确认每帧或输入变化时调用 `SetCameraFlags` 更新 `bIsSprinting`。 |
| 相机抖动 | 检查是否有其他代码也在写同一个 SpringArm / Camera / ControlRotation。 |

## 13. 当前 v1 限制

当前版本刻意保持简单：

```text
不包含自定义图编辑器
不包含 Slate / EdGraph
不包含 PlayerCameraManager 重写
不包含 Sequencer 集成
不包含网络同步
不包含复合条件表达式图
Override 同时只支持一个
Additive 支持多个
```

如果后续需要更复杂的 Transition 条件，建议先扩展 `ECameraStateTransitionRuleType` 和 `FCameraStateTransitionRule`，而不是直接引入图编辑器。

如果后续需要复杂相机演出，建议新增独立的 `UCameraLogicAsset` 子类，例如：

```text
UFinisherCameraLogic
UHitReactionCameraLogic
UDodgeEmphasisCameraLogic
UBossIntroCameraLogic
UScriptedSplineCameraLogic
```

这样可以保持核心 `CameraDirector` 稳定。
