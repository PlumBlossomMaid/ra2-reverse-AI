// ============================================================================
// RA2 生产系统算法逆向重构 (Red Alert 2: Yuri's Revenge)
// ----------------------------------------------------------------------------
// 目标二进制 : gamemd.exe (2001-10-31 原版, MSVC 6.0, x86, ImageBase 0x400000)
// 逆向工具   : Ghidra 11.1 headless 反编译 + 汇编核对
// 地址约定   : 所有地址均为 RVA (与 YRpp 地址体系一致)
//
// 本文件基于对 gamemd.exe 以下函数的反编译结果重写:
//   FactoryClass::FactoryClass        @ 0x4C98B0
//   FactoryClass::HasProgressChanged  @ 0x4C9C60
//   FactoryClass::DemandProduction    @ 0x4C9C70
//   FactoryClass::Suspend             @ 0x4C9E60
//   FactoryClass::Unsuspend           @ 0x4C9EA0
//   FactoryClass::GetBuildTimeFrames  @ 0x4C9FB0
//   FactoryClass::AbandonProduction   @ 0x4C9FF0
//   FactoryClass::GetProgress         @ 0x4CA120
//   FactoryClass::IsDone              @ 0x4CA130
//   FactoryClass::GetCostPerStep      @ 0x4CA180
//   FactoryClass::CompletedProduction @ 0x4CA1A0
//   FactoryClass::StartProduction     @ 0x4CA5A0
//   FactoryClass::RemoveOneFromQueue  @ 0x4CA620
//   FactoryClass::CountTotal          @ 0x4CA670
//   FactoryClass::IsQueued            @ 0x4CA6B0
//   TechnoClass::TimeToBuild          @ 0x6F47A0
//   HouseClass::GetPowerPercentage    @ 0x4FCE30
//   HouseClass::GetFactoryCount       @ 0x500910
//
// 设计约定: 依赖 rules.ini 的数值一律不硬编码, 通过 BuildRules 由调用方注入
// (见 BuildRules::LoadFromIni)。HouseClass/TechnoClass 等为算法所需的最小
// 数据模型, 真实游戏内嵌时由宿主替换为完整类型即可。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace ra2 {

// 生产时钟步数: 出兵卡图标 54 帧转完一圈 (汇编中 0x36)
constexpr int kFactorySteps = 54;

// ---------------------------------------------------------------------------
// 极简 INI 读取接口: 由宿主引擎实现, 算法不关心 INI 具体存储格式
// ---------------------------------------------------------------------------
struct IniReader {
    virtual ~IniReader() = default;
    virtual double get_double(const char* section, const char* key, double fallback) const = 0;
};

// ---------------------------------------------------------------------------
// [General] 建造相关规则 (对应 RulesClass 偏移, 经 Read_General 反编译确认)
// 纯数据容器: 由 LoadFromIni 从外部配置填充
// ---------------------------------------------------------------------------
struct BuildRules {
    float minLowPowerProductionSpeed = 0.0f;  // +0x570 [General]MinLowPowerProductionSpeed
    float maxLowPowerProductionSpeed = 0.0f;  // +0x574 [General]MaxLowPowerProductionSpeed
    float lowPowerPenaltyModifier    = 0.0f;  // +0x578 [General]LowPowerPenaltyModifier
    float multipleFactory            = 0.0f;  // +0x57c [General]MultipleFactory
    double wallBuildSpeedCoefficient = 0.0;   // +0x758 [General]WallBuildSpeedCoefficient
    int   buildQueueCap              = 0;     // +0xf0  建造队列上限

    // 从 INI 的 [General] 段加载。缺省回退值由调用方传入, 不内嵌于算法
    static BuildRules LoadFromIni(const IniReader& ini,
                                  double fallbackMinLowPower = 0.5,
                                  double fallbackMaxLowPower = 1.0,
                                  double fallbackPenalty = 1.0,
                                  double fallbackMultipleFactory = 1.0,
                                  double fallbackWallBuild = 1.0,
                                  int    fallbackQueueCap = 50);
};

// ---------------------------------------------------------------------------
// 生产计时器 (StageClass 布局, 来自 YRpp StageClass.h)
// 每 Rate 帧 Value 增加 Step, 累计到 kFactorySteps 即完成
// ---------------------------------------------------------------------------
struct Stage {
    int   value = 0;          // +0x00 当前进度 (0..54)
    bool  hasChanged = false; // +0x04 本帧是否推进
    int   rate = 0;           // +0x14 每步帧数
    int   step = 1;           // +0x18 每步增量
    // (中间 +0x08 处为 CDTimerClass: 由 Suspend/Unsuspend 重置)
};

// ---------------------------------------------------------------------------
// 队列 (DynamicVectorClass<TechnoTypeClass*> 语义, 原版布局 Items/Capacity/Count)
// ---------------------------------------------------------------------------
template <typename T>
struct Vector {
    T*             items = nullptr;        // +0x00
    int            capacity = 0;           // +0x04
    bool           allocated = false;      // +0x08
    int            count = 0;              // +0x10
    int            capacityIncrement = 5;  // +0x14

    int   size() const { return count; }
    T     operator[](int i) const { return items[i]; }

    // 原版逻辑: 容量不足时扩容到 count + capacityIncrement
    // 对应反编译中 DemandProduction 的扩容分支 (调用内部 Resize)
    void push_back(T item) {
        if (capacity <= count) {
            if (!allocated && capacity != 0) return;
            if (capacityIncrement < 1) return;
            T* newItems = new T[count + capacityIncrement];
            for (int i = 0; i < count; i++) newItems[i] = items[i];
            delete[] items;
            items = newItems;
            capacity = count + capacityIncrement;
            allocated = true;
        }
        items[count++] = item;
    }

    // 原版 RemoveOneFromQueue: 删除 index 处元素, 右侧元素整体左移
    void remove_at(int index) {
        if (index < 0 || index >= count) return;
        count--;
        for (int i = index; i < count; i++) {
            items[i] = items[i + 1];
        }
    }

    // 原版 StartProduction: 弹出队首 (整体左移)
    T pop_front() {
        T first = items[0];
        if (count > 0) {
            count--;
            for (int i = 0; i < count; i++) items[i] = items[i + 1];
        }
        return first;
    }
};

// ---------------------------------------------------------------------------
// 自由函数声明 (stub 类型内部引用, 先于类型定义)
// ---------------------------------------------------------------------------
struct HouseClass;
struct TechnoClass;

// TechnoClass::TimeToBuild @ 0x6F47A0 — 建造总帧数
// 由工厂 GetBuildTimeFrames 调用: rate = TimeToBuild() / 54
// rules 由调用方注入 (不硬编码)
int TimeToBuild(const TechnoClass* tech, const BuildRules& rules);

// HouseClass::GetPowerPercentage @ 0x4FCE30 — 电力充足比例 [0,1]
double GetPowerPercentage(const HouseClass* house);

// ---------------------------------------------------------------------------
// 最小数据模型
// ---------------------------------------------------------------------------
struct HouseClass {
    int  powerOutput = 0;          // +0x53A4 可用电力
    int  powerDrain = 0;           // +0x53A8 电力需求
    int  funds = 0;
    bool controlledByHuman = true;
    bool controlledByCurrentPlayer = true;
    int  aiProduceAircraft = -1;
    int  aiProduceUnit = -1;
    int  aiProduceBuilding = -1;
    int  factoryCount[8] = {};
    double diffFactor[8];          // 难度倍率表 (按类型, 对应 FUN_0050C0A0)

    HouseClass() {
        for (int i = 0; i < 8; i++) diffFactor[i] = 1.0;  // 默认无难度修正
    }

    int  availableMoney() const { return funds; }
    void giveMoney(int m) { funds += m; }
    int  GetFactoryCount(int whatAmI, bool) const { return factoryCount[whatAmI & 7]; }
    void playDeniedSound() const {}
};

struct TechnoTypeClass {
    int  id = 0;
    int  typeId = 0;            // 类型级 WhatAmI (BuildingType=7)
    int  baseBuildTime = 360;   // 基础建造帧数 (GetBuildTime)
    float speedFactor = 1.0f;   // Type+0x608
    bool unitFlag = false;      // Type+0xCCE
    bool isWall = false;
    int  cost = 100;
    int  productWhatAmI = 1;    // 产出对象 WhatAmI (Unit=1)

    int  whatAmI() const { return typeId; }
    int  actualCost() const { return cost; }
    TechnoClass* createObject(HouseClass* owner, const BuildRules* rules, bool flag);
};

struct TechnoClass {
    TechnoTypeClass* type = nullptr;
    HouseClass* owner = nullptr;
    const BuildRules* rules = nullptr;
    int  balance = 0;
    bool aiFlag = false;
    int  objWhatAmI = 0;        // 对象级 WhatAmI (Unit=1, Building=6)

    int  WhatAmI() const { return objWhatAmI; }
    int  TimeToBuild() const { return ra2::TimeToBuild(this, *rules); }
    double difficultyBuildTimeFactor() const { return owner ? owner->diffFactor[type->typeId & 7] : 1.0; }
};

// ---------------------------------------------------------------------------
// FactoryClass 生产工厂 (算法重构)
// 构造时注入 BuildRules——原版直接读全局 RulesClass::Instance, 本重构外置配置
// ---------------------------------------------------------------------------
struct FactoryClass {
    explicit FactoryClass(const BuildRules& rules) : rules_(rules) {}

    Stage                     production;     // 生产进度时钟
    Vector<TechnoTypeClass*>  queue;          // 建造队列
    TechnoClass*              object = nullptr;    // 当前生产物 (+0x58)
    bool                      isSuspended = false; // 是否挂起(没钱/手动) (+0x70)
    bool                      isManual = false;    // 挂起是否由玩家手动触发 (+0x71)
    bool                      isDifferent = false; // 进度是否发生变化 (+0x5d)
    int                       balance = 0;         // 剩余未付欠款 (+0x60)
    int                       specialItem = -1;    // 特殊生产项(超武等), -1=无 (+0x68)
    HouseClass*               owner = nullptr;     // 所属玩家 (+0x6c)
    const BuildRules&         rules_;              // 外部注入的规则数据

    // --- 已逆向算法 ---
    bool  HasProgressChanged();             // @ 0x4C9C60
    bool  DemandProduction(TechnoTypeClass* type, HouseClass* newOwner, bool startNow);
                                            // @ 0x4C9C70
    bool  Suspend(bool manual);             // @ 0x4C9E60
    bool  Unsuspend(bool manual);           // @ 0x4C9EA0
    int   GetBuildTimeFrames() const;       // @ 0x4C9FB0
    bool  AbandonProduction();              // @ 0x4C9FF0
    int   GetProgress() const { return production.value; } // @ 0x4CA120
    bool  IsDone() const;                   // @ 0x4CA130
    int   GetCostPerStep() const;           // @ 0x4CA180
    bool  CompletedProduction();            // @ 0x4CA1A0
    void  StartProduction();                // @ 0x4CA5A0
    bool  RemoveOneFromQueue(TechnoTypeClass* item); // @ 0x4CA620
    int   CountTotal(TechnoTypeClass* type) const;   // @ 0x4CA670
    bool  IsQueued(TechnoTypeClass* type) const;     // @ 0x4CA6B0
};

} // namespace ra2
