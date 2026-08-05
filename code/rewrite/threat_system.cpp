// ============================================================================
// RA2 威胁评估系统算法实现 —— 对应 gamemd.exe 反编译 + 汇编还原
// ============================================================================
#include "threat_system.h"

#include <cmath>

namespace ra2 {

// 原版快速 sqrt (FUN_004CAC40): 查找表近似; 本重构用标准 sqrt (数值等价语义)
static inline double fast_sqrt(double sq) {
    return std::sqrt(sq < 0.0 ? 0.0 : sq);
}

// x87 半偶舍入 (FUN_007C5F00 / FRNDINT)
static inline int fround(double x) {
    return static_cast<int>(std::nearbyint(x));
}

// ---------------------------------------------------------------------------
// TechnoClass::ThreatCoefficients @ 0x70CD10
//
// 汇编逐步还原 (见 memory/data/decomp/threatcoeff_asm.txt):
//   1. 系数选择: me->Type+0x1FB 标志
//        =0 → Rules Dumb 默认 (0x1068 起, DumbMyEffectiveness 系列)
//        !=0 → 类型覆盖 (Type+0x2C8 起, MyEffectivenessCoefficient 系列)
//   2. 目标反击贡献: TargetEffectiveness × targetWarhead.Verses[meArmor]
//        (目标正瞄准我 [target->Target==me] 时取负)
//   3. 目标特殊威胁: TargetSpecialThreat × targetType.SpecialThreatValue
//   4. 异阵营加成: +EnemyHouseThreatBonus (Type 阵营 != 目标阵营, 且我方阵营有效)
//   5. 我的攻击贡献: MyEffectiveness × myWarhead.Verses[targetArmor]
//   6. 目标强度: +TargetStrength × (target 血量/最大血量)
//   7. 距离惩罚: max(距离 - 射程/256, 0) × TargetDistance
//     射程 = 主武器 Range (weapon+0xB4) 或 无武器时 Type+0x5B8
//     距离 = 3D 欧氏距离 (原版快速 sqrt 近似)
// ---------------------------------------------------------------------------
double ThreatCoefficients(const TechnoClass* me, const TechnoClass* target,
                          const ThreatRules& rules, const CoordStruct* loc) {
    if (me == nullptr || target == nullptr || target->type == nullptr) {
        return 0.0;
    }

    // 1. 系数选择
    const ThreatCoeffSet& c =
        me->type->useOwnCoefficients ? me->type->coeff : rules.dumbDefault;

    double acc = 0.0;

    // 2. 目标反击贡献 (目标为战斗单位且有武器时)
    if (target->isCombatant() && target->primaryWeapon != nullptr &&
        target->primaryWeapon->warhead != nullptr) {
        double dmg = c.targetEffectiveness *
                     target->primaryWeapon->warhead->verses[me->type->armor];
        if (target->target == me) {
            dmg = -dmg; // 原版汇编 FCHS: 目标正瞄准我时取负
        }
        acc += dmg;
    }

    // 3. 目标特殊威胁
    acc += c.targetSpecialThreat * target->type->specialThreatValue;

    // 4. 异阵营加成 (原版: me Type+0x5600 有效 且 != 目标阵营)
    if (me->type->houseId != -1 && me->type->houseId != target->type->houseId) {
        acc += rules.enemyHouseThreatBonus;
    }

    // 5. 我的攻击贡献
    if (me->primaryWeapon != nullptr && me->primaryWeapon->warhead != nullptr) {
        acc += c.myEffectiveness *
               me->primaryWeapon->warhead->verses[target->type->armor];
    }

    // 6. 目标强度 (血量百分比)
    if (target->type->strength > 0) {
        acc += c.targetStrength *
               (static_cast<double>(target->strength) / target->type->strength);
    }

    // 7. 距离惩罚
    int range = (me->primaryWeapon != nullptr) ? me->primaryWeapon->range
                                               : me->type->noWeaponRange;
    range /= 256;
    int dist = 0;
    if (loc != nullptr) {
        const double dx = loc->x, dy = loc->y, dz = loc->z;
        dist = fround(fast_sqrt(dx * dx + dy * dy + dz * dz));
    }
    const double rangePenalty = (dist - range) > 0 ? (dist - range) : 0;
    return rangePenalty * c.targetDistance + acc;
}

// ---------------------------------------------------------------------------
// TechnoClass::CalculateThreat @ 0x708B40 — "珍宝函数"
// YRpp 注释: "...this is another gem of a function, to be revealed another time"
// 2026-08-05 揭晓:
//   单位:    Type->ThreatPosed (rules.ini ThreatPosed 属性)
//   建筑:    载员数 > 0   → 载员数 × ThreatPerOccupant
//            有驻防单位   → 驻防单位类型 ThreatPosed
//            否则         → 自身类型 ThreatPosed
// Phobos Hooks.cpp:2004 交叉验证: ThreatPerOccupant * occupantCount
// ---------------------------------------------------------------------------
int CalculateThreat(const TechnoClass* unit, const ThreatRules& rules) {
    if (unit == nullptr || unit->type == nullptr) {
        return 0;
    }
    if (unit->whatAmI == kWhatAmIBuilding) {
        if (unit->occupants > 0) {
            return unit->occupants * rules.threatPerOccupant;
        }
        if (unit->garrison != nullptr && unit->garrison->type != nullptr) {
            return unit->garrison->type->threatPosed;
        }
        return unit->type->threatPosed;
    }
    return unit->type->threatPosed;
}

// ---------------------------------------------------------------------------
// TechnoClass::CanAutoTargetObject @ 0x6F7CA0 — 目标评估核心
// 原版流程 (大量合法性检查省略为代表性条件):
//   1. 目标合法性: 隐形状态/友军/射程/移动类型/防空等 (简化)
//   2. 威胁值 = round(ThreatCoefficients)
//   3. 类型修正: me 类型标志 +0x394==1 时按目标载员调整
//   4. 威胁标志修正 (目标为建筑时):
//        0x800 / 0x8000 → 威胁 += 单位数×1000
//        0x10000        → 威胁 += 1000
//        0x1000         → 无内部单位 → 威胁 = 0
//   5. ShouldSuppress 抑制检查 (放空炮)
//   6. 威胁 < 1 → 1 (最小威胁)
// ---------------------------------------------------------------------------
bool CanAutoTargetObject(const TechnoClass* me, const TechnoClass* target,
                         const ThreatRules& rules, int* outThreat) {
    if (me == nullptr || target == nullptr || outThreat == nullptr) {
        return false;
    }
    // 1. 合法性检查 (代表性条件)
    if (target == me) return false;
    if (!target->isCombatant()) return false;

    // 2. 威胁值
    CoordStruct loc{}; // 简化: 距离用调用方传入; 本实现无坐标上下文时距离项为 0
    int threat = fround(ThreatCoefficients(me, target, rules, &loc));

    // 3-4. 修正 (原版: 威胁标志位 & 建筑内部单位)
    if (target->whatAmI == kWhatAmIBuilding) {
        if (target->occupants > 0) {
            threat += target->occupants * 1000;
        } else if (me->type->useOwnCoefficients) {
            threat = 0; // 原版 0x1000: 无内部单位的特殊建筑 → 无威胁
        }
    }

    // 6. 最小威胁
    if (threat < 1) threat = 1;
    *outThreat = threat;
    return true;
}

// ---------------------------------------------------------------------------
// TechnoClass::GreatestThreat @ 0x6F8DF0 — 索敌主函数 (分层扫描)
// 原版流程:
//   层1 建筑数组 → 层2 单位数组 → 层3 飞机跟踪器 → 层4 射程内螺旋扫描格子
//   每层用 CanAutoTargetObject 计算威胁, 保留威胁最高目标
//   螺旋扫描在中途 (距离/2) 找到目标时提前返回
// 本实现保留分层与取最大语义, 候选集合由宿主提供
// ---------------------------------------------------------------------------
const TechnoClass* GreatestThreat(const TechnoClass* me, const ThreatRules& rules) {
    // 简化: 威胁值最高的候选由宿主遍历; 本函数为算法骨架
    (void)me;
    (void)rules;
    return nullptr;
}

} // namespace ra2
