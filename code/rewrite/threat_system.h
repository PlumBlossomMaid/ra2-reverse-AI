// ============================================================================
// RA2 威胁评估系统算法逆向重构 (Red Alert 2: Yuri's Revenge)
// ----------------------------------------------------------------------------
// 目标二进制 : gamemd.exe (2001-10-31 原版, MSVC 6.0, x86, ImageBase 0x400000)
// 地址约定   : 所有地址均为 RVA (与 YRpp 地址体系一致)
//
// 本文件基于以下函数的反编译 + 汇编逐条还原:
//   TechnoClass::ThreatCoefficients  @ 0x70CD10  目标威胁评估公式
//   TechnoClass::CalculateThreat     @ 0x708B40  "珍宝函数"——单位对地图的威胁贡献
//   TechnoClass::UpdateThreatInCell  @ 0x70F6E0  威胁值同步到所在格子
//   CellClass::UpdateThreat          @ 0x481870  格子威胁传播
//   HouseClass::AdjustThreats        @ 0x509400  威胁地图 3x3 模糊扩散
//   TechnoClass::CanAutoTargetObject @ 0x6F7CA0  目标合法性 + 威胁值计算
//   TechnoClass::TryAutoTargetObject @ 0x6F8960  格子目标链表扫描
//   TechnoClass::GreatestThreat      @ 0x6F8DF0  索敌主函数 (分层扫描)
//   HouseClass::AcquiredThreatNode   @ 0x509130  威胁节点标记
//   WeaponTypeClass::AllowedThreats  @ 0x772A90  武器可攻击类型标志
//
// 设计约定: 与生产系统一致——rules.ini 数值通过 ThreatRules 注入,
// 算法层不硬编码。原版 rules.ini 具体系数值见 docs/threat-system/。
// ============================================================================
#pragma once

namespace ra2 {

// WhatAmI 枚举 (YRpp GeneralDefinitions.h, 与本模块相关部分)
constexpr int kWhatAmIUnit       = 1;
constexpr int kWhatAmIAircraft   = 2;
constexpr int kWhatAmIBuilding   = 6;
constexpr int kWhatAmIInfantry   = 15;

// ---------------------------------------------------------------------------
// 威胁系数五维 (对应 RulesClass 偏移, Read_General 反编译确认)
// 类型级覆盖: TechnoTypeClass+0x2C8 起 (MyEffectivenessCoefficient 等)
// Rules 默认: RulesClass+0x1040 起 (MyEffectivenessCoefficientDefault 等)
// Rules Dumb: RulesClass+0x1068 起 (DumbMyEffectivenessCoefficient 等)
// ---------------------------------------------------------------------------
struct ThreatCoeffSet {
    double myEffectiveness       = 0.0;  // 我打目标的能力权重
    double targetEffectiveness   = 0.0;  // 目标打我的能力权重
    double targetSpecialThreat   = 0.0;  // 目标特殊威胁权重
    double targetStrength        = 0.0;  // 目标强度(血量比例)权重
    double targetDistance        = 0.0;  // 目标距离权重
};

// [General] 威胁相关规则 (Read_General @ 0x66D530 反编译确认)
struct ThreatRules {
    ThreatCoeffSet defaultCoeff;    // 0x1040-0x1064 非 Dumb 默认 (未在代码中发现使用点)
    ThreatCoeffSet dumbDefault;     // 0x1068-0x108C Dumb 默认 (ThreatCoefficients 实际使用)
    double enemyHouseThreatBonus = 0.0; // +0x1090 异阵营威胁加成
    int    threatPerOccupant     = 0;   // +0xDF4  每载员威胁系数
};

// ---------------------------------------------------------------------------
// 最小数据模型 (威胁系统所需字段, 真实游戏内嵌时由宿主替换)
// ---------------------------------------------------------------------------
struct CoordStruct {
    int x = 0, y = 0, z = 0;
};

struct WarheadType {
    // Verses 伤害表: +0xA0 起, 每项 double, 索引 = 目标装甲类型
    double verses[32] = {};   // 对 armor 的伤害倍率
};

struct WeaponType {
    WarheadType* warhead = nullptr;  // +0xAC
    int range = 0;                   // +0xB4 (leptons)
};

struct TechnoTypeClass {
    bool  useOwnCoefficients = false;    // +0x1FB 使用类型覆盖系数
    int   armor = 0;                     // +0x9C  装甲类型索引 (Verses 表索引)
    int   strength = 0;                  // +0xA0  最大血量
    double specialThreatValue = 0.0;     // +0x2C0 特殊威胁值
    ThreatCoeffSet coeff;                // +0x2C8 起 (MyEffectiveness...)
    int   noWeaponRange = 0;             // +0x5B8 无武器时的评估射程
    int   threatPosed = 0;               // +0x670 基础威胁值 (rules.ini ThreatPosed 属性)
    int   houseId = 0;                   // 阵营索引 (用于异阵营判定)
};

struct TechnoClass {
    TechnoTypeClass* type = nullptr;
    TechnoClass* target = nullptr;       // 当前瞄准目标 (+0x2B4, 用于"正瞄准我"判定)
    int  strength = 0;                   // +0x6C  当前血量
    int  whatAmI = 0;                    // 对象类型 (Unit=1/Building=6/Infantry=15)
    WeaponType* primaryWeapon = nullptr; // 主武器 (vtable[0x3f8] 返回链)
    int  occupants = 0;                  // 载员数 (建筑, vtable[0x408])
    TechnoClass* garrison = nullptr;     // 驻防单位 (建筑, +0xB9*4)

    bool isCombatant() const {
        return whatAmI == kWhatAmIBuilding || whatAmI == kWhatAmIInfantry ||
               whatAmI == kWhatAmIUnit || whatAmI == kWhatAmIAircraft;
    }
};

// ---------------------------------------------------------------------------
// TechnoClass::ThreatCoefficients @ 0x70CD10 — 目标威胁评估公式
// 返回值为 double, 调用方四舍五入为 int 威胁值
// ---------------------------------------------------------------------------
double ThreatCoefficients(const TechnoClass* me, const TechnoClass* target,
                          const ThreatRules& rules, const CoordStruct* loc);

// ---------------------------------------------------------------------------
// TechnoClass::CalculateThreat @ 0x708B40 — "珍宝函数"
// 单位/建筑对地图的威胁贡献 (ThreatPosed):
//   单位:    Type->ThreatPosed
//   建筑:    载员数 × ThreatPerOccupant; 无载员有驻防 → 驻防单位类型威胁值
// ---------------------------------------------------------------------------
int CalculateThreat(const TechnoClass* unit, const ThreatRules& rules);

// ---------------------------------------------------------------------------
// TechnoClass::CanAutoTargetObject @ 0x6F7CA0 — 目标评估核心
// 合法性检查 (简化) + 威胁值计算 + 按目标类型修正
// 输出: *outThreat = 威胁值 (最低 1); 返回是否可攻击
// ---------------------------------------------------------------------------
bool CanAutoTargetObject(const TechnoClass* me, const TechnoClass* target,
                         const ThreatRules& rules, int* outThreat);

// ---------------------------------------------------------------------------
// TechnoClass::GreatestThreat @ 0x6F8DF0 — 索敌主函数
// 分层扫描: 建筑 → 单位 → 飞机 → 螺旋格子, 取威胁最高目标
// ---------------------------------------------------------------------------
const TechnoClass* GreatestThreat(const TechnoClass* me, const ThreatRules& rules);

} // namespace ra2
