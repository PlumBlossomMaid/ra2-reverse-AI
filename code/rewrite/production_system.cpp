// ============================================================================
// RA2 生产系统算法实现 —— 对应 gamemd.exe 反编译结果
// 各函数顶部标注原版地址, 关键算法附汇编/反编译依据
// ============================================================================
#include "production_system.h"

#include <cmath>

namespace ra2 {

// ---------------------------------------------------------------------------
// x87 FRNDINT 舍入: 原版 TimeToBuild 内 FUN_007C5F00 用 ROUND(ST0),
// 默认 RC=0 (round to nearest, ties to even)。std::nearbyint 与此一致。
// ---------------------------------------------------------------------------
static inline int fround(double x) {
    return static_cast<int>(std::nearbyint(x));
}

// AbstractType 枚举值 (YRpp GeneralDefinitions.h)
constexpr int kWhatAmIUnit       = 1;
constexpr int kWhatAmIAircraft   = 2;
constexpr int kWhatAmIAircraftType = 3;
constexpr int kWhatAmIBuilding   = 6;
constexpr int kWhatAmIBuildingType = 7;
constexpr int kWhatAmIInfantryType = 16;

// ---------------------------------------------------------------------------
// BuildRules::LoadFromIni — 从外部 INI 加载 [General] 建造规则
// 缺省回退值作为参数由调用方传入: 即"原版行为"数值的决策权在宿主, 不在算法
// 对应 RulesClass::Read_General @ 0x66D530 中的读取点
// ---------------------------------------------------------------------------
BuildRules BuildRules::LoadFromIni(const IniReader& ini,
                                   double fallbackMinLowPower,
                                   double fallbackMaxLowPower,
                                   double fallbackPenalty,
                                   double fallbackMultipleFactory,
                                   double fallbackWallBuild,
                                   int fallbackQueueCap) {
    BuildRules r;
    const char* sec = "General";
    r.minLowPowerProductionSpeed = static_cast<float>(
        ini.get_double(sec, "MinLowPowerProductionSpeed", fallbackMinLowPower));
    r.maxLowPowerProductionSpeed = static_cast<float>(
        ini.get_double(sec, "MaxLowPowerProductionSpeed", fallbackMaxLowPower));
    r.lowPowerPenaltyModifier = static_cast<float>(
        ini.get_double(sec, "LowPowerPenaltyModifier", fallbackPenalty));
    r.multipleFactory = static_cast<float>(
        ini.get_double(sec, "MultipleFactory", fallbackMultipleFactory));
    r.wallBuildSpeedCoefficient =
        ini.get_double(sec, "WallBuildSpeedCoefficient", fallbackWallBuild);
    r.buildQueueCap = fallbackQueueCap;
    return r;
}

// ---------------------------------------------------------------------------
// 最小数据模型的宿主实现: 创建生产物对象 (对应原版 type->CreateObject)
// ---------------------------------------------------------------------------
TechnoClass* TechnoTypeClass::createObject(HouseClass* owner,
                                           const BuildRules* rules, bool /*flag*/) {
    auto* t = new TechnoClass();
    t->type = this;
    t->owner = owner;
    t->rules = rules;
    t->objWhatAmI = productWhatAmI;
    return t;
}

// ---------------------------------------------------------------------------
// HouseClass::GetPowerPercentage @ 0x4FCE30
// 汇编: available<drain 时 FILD/FIDIV 返回 available/drain (double);
//       available==0 返回 double 0.0 (0x7E2800); 否则返回 double 1.0 (0x7E1718)
// ---------------------------------------------------------------------------
double GetPowerPercentage(const HouseClass* house) {
    const int available = house->powerOutput; // +0x53A4
    const int drain     = house->powerDrain;  // +0x53A8
    if (available < drain && drain != 0) {
        if (available != 0) {
            return static_cast<double>(available) / static_cast<double>(drain);
        }
        return 0.0;
    }
    return 1.0;
}

// ---------------------------------------------------------------------------
// FactoryClass::HasProgressChanged @ 0x4C9C60
// 读取并清零 isDifferent 标志 (供 UI 检测进度是否变化)
// ---------------------------------------------------------------------------
bool FactoryClass::HasProgressChanged() {
    const bool changed = isDifferent;
    isDifferent = false;
    return changed;
}

// ---------------------------------------------------------------------------
// FactoryClass::GetBuildTimeFrames @ 0x4C9FB0
// rate = TimeToBuild() / 54, 钳制到 [1, 255]
// ---------------------------------------------------------------------------
int FactoryClass::GetBuildTimeFrames() const {
    int frames = 0;
    if (object != nullptr) {
        frames = object->TimeToBuild();
    }
    frames /= kFactorySteps;
    if (frames < 1) return 1;
    if (frames > 255) return 255;
    return frames;
}

// ---------------------------------------------------------------------------
// FactoryClass::GetCostPerStep @ 0x4CA180
// 每步扣款 = Balance / (54 - value); 完成时返回剩余 Balance
// ---------------------------------------------------------------------------
int FactoryClass::GetCostPerStep() const {
    if (object == nullptr) {
        return 0;
    }
    const int remainingSteps = kFactorySteps - production.value;
    if (remainingSteps != 0) {
        return balance / remainingSteps;
    }
    return balance;
}

// ---------------------------------------------------------------------------
// FactoryClass::Suspend @ 0x4C9E60
// 挂起生产: 记 manual 标志, 清零 Rate 并重置计时器 (进度冻结)
// ---------------------------------------------------------------------------
bool FactoryClass::Suspend(bool manual) {
    if (isSuspended) {
        return false;
    }
    isManual = manual;
    isSuspended = true;
    production.rate = 0;      // Rate = 0
    // 原版在此重置 CDTimerClass (0x2C/0x30/0x34)
    return true;
}

// ---------------------------------------------------------------------------
// FactoryClass::Unsuspend @ 0x4C9EA0
// 恢复生产: 重算 Rate = TimeToBuild/54 与每步成本, 检查资金是否足够
//
// 反编译要点:
//   1. 仅当存在生产物/特殊项、当前挂起、且未完成时有效
//   2. 恢复后每步成本 = Balance / (54 - value), 需要 <= 可用资金
//   3. 原版在函数入口即清除 isSuspended——资金不足时也返回 false,
//      由调用方决定是否重新挂起 (此行为按原样转写)
// ---------------------------------------------------------------------------
bool FactoryClass::Unsuspend(bool manual) {
    const bool hasProduction = (object != nullptr || specialItem != 0);
    const bool finished = (production.value == kFactorySteps);
    if (!hasProduction || !isSuspended || finished) {
        return false;
    }

    isSuspended = false;

    // 重算每步帧数
    int rate = 0;
    if (object != nullptr) {
        rate = object->TimeToBuild();
    }
    rate /= kFactorySteps;
    if (rate < 1) rate = 1;
    else if (rate > 255) rate = 255;
    production.rate = rate;
    // 原版在此重置 CDTimerClass

    // 每步成本
    int costPerStep;
    if (object == nullptr) {
        costPerStep = 0;
    } else {
        const int remainingSteps = kFactorySteps - production.value;
        costPerStep = (remainingSteps == 0) ? balance : balance / remainingSteps;
    }

    // 资金检查: 每步成本 <= 可用资金才恢复 (反编译: iVar3 <= availableMoney)
    if (costPerStep <= owner->availableMoney()) {
        isManual = true;
        // 原版 quirk: 若为玩家手动操作, 恢复后立即重新挂起
        if (manual) {
            isManual = true;
            isSuspended = true;
            production.rate = 0;
            // 原版在此重置 CDTimerClass
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FactoryClass::CompletedProduction @ 0x4CA1A0
// 进度达到 54 时: 清除生产物/特殊项, 标记挂起(等待部署), 重置时钟
// 注意: 特殊项判定用 != 0 (与 IsDone 的 != -1 不同, 原版如此)
// ---------------------------------------------------------------------------
bool FactoryClass::CompletedProduction() {
    if (object != nullptr && production.value == kFactorySteps) {
        object = nullptr;
        isSuspended = true;
        isDifferent = true;
        production.value = 0;
        production.rate = 0; // 原版重置计时器并清零 Rate
        return true;
    }
    if (specialItem != 0 && production.value == kFactorySteps) {
        specialItem = -1;
        isSuspended = true;
        isDifferent = true;
        production.value = 0;
        production.rate = 0;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FactoryClass::IsDone @ 0x4CA130
// ---------------------------------------------------------------------------
bool FactoryClass::IsDone() const {
    if (object != nullptr && production.value == kFactorySteps) {
        return true;
    }
    if (specialItem != -1 && production.value == kFactorySteps) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FactoryClass::AbandonProduction @ 0x4C9FF0
// 放弃当前生产: 退款 = 实际成本 - 已投入 (Balance 为剩余欠款), 清除状态
// ---------------------------------------------------------------------------
bool FactoryClass::AbandonProduction() {
    if (object == nullptr) {
        return false;
    }
    // 原版: 日志 "Abandoning production of <ID>"
    // 退款 = GetActualCost() - balance (balance 是尚未扣除的欠款)
    const int actualCost = object->type->actualCost();
    owner->giveMoney(actualCost - balance);
    balance = 0;
    if (specialItem != 0) {
        specialItem = -1;
    }
    production.rate = 0;
    production.value = 0;
    // 原版在此重置 CDTimerClass
    isSuspended = true;
    isDifferent = true;

    // AI 清理: 非人类玩家放弃时清空对应类型的生产标记 (反编译 switch)
    if (!owner->controlledByHuman) {
        switch (object->WhatAmI()) {
        case kWhatAmIAircraft:   owner->aiProduceAircraft = -1; break;
        case kWhatAmIUnit:       owner->aiProduceUnit     = -1; break;
        case kWhatAmIBuilding:   owner->aiProduceBuilding = -1; break;
        case kWhatAmIAircraftType: /* BuildingClass 分支: 0x564C */ break;
        default: break;
        }
    }
    // 原版: 释放 Object 并置空
    object = nullptr;
    return true;
}

// ---------------------------------------------------------------------------
// FactoryClass::StartProduction @ 0x4CA5A0
// 从队列弹出队首并开始生产 (创建生产物)
// ---------------------------------------------------------------------------
void FactoryClass::StartProduction() {
    if (queue.size() == 0 || object != nullptr) {
        return;
    }
    if (production.rate != 0 && !isSuspended) {
        return; // 原版: Rate 未清零且未挂起时不启动 (防止进度丢失)
    }
    TechnoTypeClass* type = queue.pop_front();
    // 原版: type->CreateObject(...) + FUN_004FA350 初始化 (含 0xCCE 标志传递)
    object = type->createObject(owner, &rules_, type->unitFlag);
    if (object != nullptr) {
        object->balance = type->actualCost();
        balance = type->actualCost();
    }
}

// ---------------------------------------------------------------------------
// FactoryClass::DemandProduction @ 0x4C9C70
// 请求生产: 立即开始 / 入队 / 拒绝
//
// 反编译要点 (条件分支):
//   - 请求物是 BuildingType(7) 时先放弃当前生产 (同类型建筑替换)
//   - 立即开始条件: 工厂空闲(Object==null && 队列空 && (Rate==0 || 挂起))
//                    或 startNow 参数为 true
//   - 队列满 (buildQueueCap) 或已排队过 → 拒绝并播放提示音
// ---------------------------------------------------------------------------
bool FactoryClass::DemandProduction(TechnoTypeClass* type, HouseClass* newOwner,
                                    bool startNow) {
    if (type->whatAmI() == kWhatAmIBuildingType) {
        AbandonProduction(); // 建筑类先取消当前 (替换生产)
    }

    const bool factoryIdle =
        (production.rate == 0 || isSuspended) &&
        queue.size() < 1 &&
        (object == nullptr || !isSuspended);

    if (type->whatAmI() == kWhatAmIBuildingType || factoryIdle || startNow) {
        // ---- 立即开始生产 ----
        isDifferent = true;
        isSuspended = true;
        production.rate = 0;
        production.value = 0;
        // 原版在此重置 CDTimerClass
        object = type->createObject(newOwner, &rules_, type->unitFlag);
        if (object == nullptr) {
            return false;
        }
        // AI 建筑标记
        if (!newOwner->controlledByHuman && object->WhatAmI() == kWhatAmIBuilding) {
            object->aiFlag = 1; // 原版: Object+0x6CA = 1
        }
        owner = newOwner;
        balance = type->actualCost();
        object->balance = balance; // 原版: Object+0x300 = balance
        return true;
    }

    // ---- 入队 ----
    if (rules_.buildQueueCap <= queue.size() || IsQueued(type)) {
        if (newOwner->controlledByCurrentPlayer) {
            newOwner->playDeniedSound(); // 原版: FUN_00750920(1.0f, 0)
        }
        return false;
    }
    queue.push_back(type);
    return true;
}

// ---------------------------------------------------------------------------
// FactoryClass::RemoveOneFromQueue @ 0x4CA620
// 从队列移除指定类型的一项 (线性查找 + 左移)
// ---------------------------------------------------------------------------
bool FactoryClass::RemoveOneFromQueue(TechnoTypeClass* item) {
    for (int i = 0; i < queue.size(); i++) {
        if (queue[i] == item) {
            queue.remove_at(i);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// FactoryClass::CountTotal @ 0x4CA670
// 计数: 正在生产的 + 队列中的 (相同类型)
// ---------------------------------------------------------------------------
int FactoryClass::CountTotal(TechnoTypeClass* type) const {
    int total = 0;
    if (object != nullptr && object->type == type) {
        total = 1;
    }
    for (int i = 0; i < queue.size(); i++) {
        if (queue[i] == type) {
            total++;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// FactoryClass::IsQueued @ 0x4CA6B0
// 队列中是否至少有一项 (不含正在生产的)
// ---------------------------------------------------------------------------
bool FactoryClass::IsQueued(TechnoTypeClass* type) const {
    for (int i = 0; i < queue.size(); i++) {
        if (queue[i] == type) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// TechnoClass::TimeToBuild @ 0x6F47A0 — 建造总帧数
//
// 汇编逐步还原 (见 timetobuild_asm.txt):
//   [0x6F47A7]  EAX = this->vtable; CALL [EAX+0x88]   → Type 指针
//   [0x6F47B1]  CALL [Type->vtable+0x88]              → 基础帧数 (GetBuildTime)
//   [0x6F47C1]  CALL [this->vtable+0x84]              → GetType()
//   [0x6F47CE]  CALL 0x50C0A0                         → 难度倍率 (按 WhatAmI 查表)
//   [0x6F47D3]  FIMUL baseFrames                       → frames × 难度倍率
//   [0x6F47E4]  CALL [this->vtable+0x84]              → GetType()
//   [0x6F47EE]  FMUL [Type+0x608]                     → frames × 类型速度系数
//   [0x6F4803]  CALL 0x4FCE30                         → GetPowerPercentage()
//   [0x6F480C]  ST0=1.0; FSUB power
//   [0x6F481C]  FMUL [Rules+0x578]                    → (1-power)×LowPowerPenaltyModifier
//   [0x6F4822]  FSUBR 1.0                             → f = 1-(1-power)×Penalty
//   [0x6F4828..] 钳制到 [MinLowPower(0x570), MaxLowPower(0x574)]
//   [0x6F4884]  FDIV frames / f                       → 低电建造变慢
//   [0x6F489C]  WhatAmI()==Unit(1) && GetType()       → 取 Type+0xCCE 标志
//   [0x6F48CF]  GetFactoryCount(WhatAmI, flag)        → 工厂数量
//   [0x6F48E5..] 若 MultipleFactory(0x57C)!=0, 每额外工厂 frames×=MultipleFactory
//   [0x6F491E]  WhatAmI()==Building(6) && Type+0x1571(墙) → frames×=WallBuildSpeed(0x758,double)
// ---------------------------------------------------------------------------
int TimeToBuild(const TechnoClass* tech, const BuildRules& rules) {
    // 1. 基础帧数 (原版: vtable 链最终取 Type 的 GetBuildTime)
    int frames = tech->type->baseBuildTime;

    // 2. 难度倍率 (FUN_0050C0A0 @ 0x50C0A0: 按 WhatAmI 从 House 难度表读取 float,
    //    步兵/飞机/建筑各有槽位, 默认 1.0; 本重构以 house 方法抽象)
    frames = fround(frames * tech->difficultyBuildTimeFactor());

    // 3. 类型速度系数 (Type +0x608)
    frames = fround(frames * tech->type->speedFactor);

    // 4. 电力影响
    const double power = GetPowerPercentage(tech->owner); // [0,1]
    float f = 1.0f - static_cast<float>((1.0 - power) * rules.lowPowerPenaltyModifier);
    if (f < rules.minLowPowerProductionSpeed) {
        f = rules.minLowPowerProductionSpeed;
    }
    if (power < 1.0 && f > rules.maxLowPowerProductionSpeed) {
        f = rules.maxLowPowerProductionSpeed;
    }
    if (f == 0.0f) {
        f = 0.01f; // 防除零 (原版常量 0x7F4E34 = 0.01f)
    }
    frames = fround(frames / f);

    // 5. 多工厂减速: 每个额外工厂 frames ×= MultipleFactory
    const int whatAmI = tech->WhatAmI();
    const bool unitFlag =
        (whatAmI == kWhatAmIUnit && tech->type != nullptr) ? tech->type->unitFlag : false;
    const int factoryCount = tech->owner->GetFactoryCount(whatAmI, unitFlag);
    if (rules.multipleFactory != 0.0f && factoryCount > 1) {
        for (int i = 1; i < factoryCount; i++) {
            frames = fround(frames * rules.multipleFactory);
        }
    }

    // 6. 围墙加速 (Building 且是墙 → × WallBuildSpeedCoefficient)
    if (whatAmI == kWhatAmIBuilding && tech->type != nullptr && tech->type->isWall) {
        frames = fround(frames * rules.wallBuildSpeedCoefficient);
    }

    return frames;
}

} // namespace ra2
