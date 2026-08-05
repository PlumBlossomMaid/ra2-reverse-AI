// ============================================================================
// RA2 威胁评估系统算法测试
// 数值来源: 原版 rulesmd.ini [General] (Official Rules of Engagement, YR)
//   ThreatPerOccupant=10
//   MyEffectivenessCoefficientDefault=200   TargetEffectivenessCoefficientDefault=-200
//   TargetSpecialThreatCoefficientDefault=200  TargetStrengthCoefficientDefault=-200
//   TargetDistanceCoefficientDefault=-10
//   DumbMyEffectivenessCoefficient=200  DumbTargetEffectivenessCoefficient=200
//   DumbTargetSpecialThreatCoefficient=200  DumbTargetStrengthCoefficient=200
//   DumbTargetDistanceCoefficient=-1
//   EnemyHouseThreatBonus=400
// 编译: cl /std:c++17 /utf-8 /W4 threat_system.cpp demo_threat.cpp /Fe:demo_threat.exe
// ============================================================================
#include "threat_system.h"

#include <cmath>
#include <cstdio>

using namespace ra2;

static int s_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) { printf("PASS  %s\n", msg); }                               \
        else { printf("FAIL  %s\n", msg); s_failures++; }                      \
    } while (0)

#define CHECK_EQ(a, b, msg)                                                    \
    do {                                                                       \
        int va = (int)std::nearbyint(a), vb = (int)std::nearbyint(b);          \
        if (va == vb) { printf("PASS  %s (%d)\n", msg, va); }                  \
        else { printf("FAIL  %s got=%d want=%d\n", msg, va, vb); s_failures++; } \
    } while (0)

#define CHECK_GT(a, b, msg)                                                    \
    do {                                                                       \
        double va = (a), vb = (b);                                             \
        if (va > vb) { printf("PASS  %s (%.3f > %.3f)\n", msg, va, vb); }      \
        else { printf("FAIL  %s (%.3f <= %.3f)\n", msg, va, vb); s_failures++; } \
    } while (0)

// 原版 rulesmd.ini [General] 威胁系数 (硬编码)
static ThreatRules make_rules() {
    ThreatRules r;
    r.dumbDefault.myEffectiveness = 200.0;
    r.dumbDefault.targetEffectiveness = 200.0;
    r.dumbDefault.targetSpecialThreat = 200.0;
    r.dumbDefault.targetStrength = 200.0;
    r.dumbDefault.targetDistance = -1.0;
    r.enemyHouseThreatBonus = 400.0;
    r.threatPerOccupant = 10;
    return r;
}

static ThreatRules make_rules_with_own() {
    ThreatRules r = make_rules();
    r.defaultCoeff.myEffectiveness = 200.0;
    r.defaultCoeff.targetEffectiveness = -200.0;
    r.defaultCoeff.targetSpecialThreat = 200.0;
    r.defaultCoeff.targetStrength = -200.0;
    r.defaultCoeff.targetDistance = -10.0;
    return r;
}

static TechnoTypeClass make_type(int threatPosed, int armor, int strength,
                                 int houseId, double specialThreat = 0.0) {
    TechnoTypeClass t;
    t.threatPosed = threatPosed;
    t.armor = armor;
    t.strength = strength;
    t.houseId = houseId;
    t.specialThreatValue = specialThreat;
    return t;
}

// 组装: 我方(有武器, 类型覆盖系数) vs 目标(有武器, 异阵营)
static void make_combatants(TechnoClass& me, TechnoClass& target, TechnoTypeClass& myType,
                            TechnoTypeClass& targetType, WarheadType& myWH,
                            WarheadType& tgtWH, WeaponType& myWeapon, WeaponType& tgtWeapon) {
    myType = make_type(0, 0, 100, 1);
    targetType = make_type(0, 0, 100, 2);

    myWH.verses[0] = 100.0;   // 我对 armor 0 的伤害倍率
    myWeapon.warhead = &myWH;
    myWeapon.range = 256;

    tgtWH.verses[0] = 50.0;   // 目标对我 armor 0 的伤害倍率
    tgtWeapon.warhead = &tgtWH;
    tgtWeapon.range = 256;

    me.type = &myType;
    me.primaryWeapon = &myWeapon;
    me.strength = 100;

    target.type = &targetType;
    target.primaryWeapon = &tgtWeapon;
    target.strength = 100;
    target.whatAmI = kWhatAmIUnit;
}

static void test_threat_coefficients() {
    printf("== ThreatCoefficients (原版数值) ==\n");

    // 场景A: 类型覆盖系数 (myEff=200, tgtEff=-200, special=200, strength=-200, dist=-10)
    //   200×100(我打它) -200×50(它打我) +200×0(特殊) +400(异阵营) -200×1.0(强度) + 0(距离)
    //   = 20000 - 10000 + 0 + 400 - 200 = 10200
    auto rules = make_rules_with_own();
    TechnoClass me, target;
    TechnoTypeClass myType, targetType;
    WarheadType myWH, tgtWH;
    WeaponType myWeapon, tgtWeapon;
    make_combatants(me, target, myType, targetType, myWH, tgtWH, myWeapon, tgtWeapon);
    me.type->useOwnCoefficients = true;
    me.type->coeff = rules.defaultCoeff;

    CoordStruct loc{};
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 10200, "异阵营双方武器: 10200");

    // 场景B: 距离 512 (射程 256/256=1 格, 超出 511 → 511×(-10)=-5110)
    loc.x = 512;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 5090, "距离惩罚: 5090");
    loc = {};

    // 场景C: 同阵营 → 无加成 (20000-10000-200=9800)
    target.type->houseId = 1;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 9800, "同阵营无加成: 9800");
    target.type->houseId = 2;

    // 场景D: 特殊威胁 500 → +200×500=100000 (110200)
    targetType.specialThreatValue = 500.0;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 110200, "特殊威胁加成: 110200");
    targetType.specialThreatValue = 0.0;

    // 场景E: 类型覆盖 MyEffectiveness=400 → 400×100-10000+400-200=30200
    me.type->coeff.myEffectiveness = 400.0;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 30200, "类型覆盖系数生效: 30200");
    me.type->coeff = rules.defaultCoeff;

    // 场景F: Dumb 系数 (无武器, Dumb: eff=200×50 + bonus 400 + strength 200×1.0 = 10600)
    me.primaryWeapon = nullptr;
    me.type->useOwnCoefficients = false;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), 10600, "Dumb 系数生效: 10600");

    // 场景G: 目标瞄准我 → 目标反击贡献取负 (me 无武器沿用场景F)
    //   -200×50(取负) + 400 + 200 = -9400
    target.target = &me;
    CHECK_EQ(ThreatCoefficients(&me, &target, rules, &loc), -9400, "目标瞄准我取负: -9400");
}

static void test_calculate_threat() {
    printf("== CalculateThreat (珍宝函数, ThreatPerOccupant=10) ==\n");
    auto rules = make_rules();

    // 单位: 威胁 = Type->ThreatPosed
    TechnoTypeClass unitType = make_type(25, 0, 100, 1);
    TechnoClass unit;
    unit.type = &unitType;
    unit.whatAmI = kWhatAmIUnit;
    CHECK(CalculateThreat(&unit, rules) == 25, "单位威胁 = Type.ThreatPosed (25)");

    // 建筑: 载员数 × ThreatPerOccupant(10)
    TechnoTypeClass bldType = make_type(10, 0, 100, 1);
    TechnoClass building;
    building.type = &bldType;
    building.whatAmI = kWhatAmIBuilding;
    building.occupants = 4;
    CHECK(CalculateThreat(&building, rules) == 40, "建筑威胁 = 载员×10 (4×10)");

    // 驻防建筑: 驻防单位类型威胁
    building.occupants = 0;
    TechnoClass garrison;
    garrison.type = &unitType; // ThreatPosed=25
    building.garrison = &garrison;
    CHECK(CalculateThreat(&building, rules) == 25, "驻防建筑威胁 = 驻防单位类型威胁 (25)");

    // 空建筑: 自身类型威胁
    building.garrison = nullptr;
    CHECK(CalculateThreat(&building, rules) == 10, "空建筑威胁 = 自身类型威胁 (10)");
}

static void test_can_auto_target() {
    printf("== CanAutoTargetObject (原版数值) ==\n");
    auto rules = make_rules();

    // 无武器同阵营 (Dumb: 强度 200×1.0 = 200 → 威胁 200)
    TechnoTypeClass myType = make_type(0, 0, 100, 1);
    TechnoClass me;
    me.type = &myType;
    me.strength = 100;

    TechnoTypeClass targetType = make_type(0, 0, 100, 1); // 同阵营
    TechnoClass target;
    target.type = &targetType;
    target.whatAmI = kWhatAmIUnit;
    target.strength = 100;

    int threat = 0;
    CHECK(CanAutoTargetObject(&me, &target, rules, &threat), "目标可攻击");
    CHECK(threat >= 1, "威胁值下限 = 1");
    CHECK_EQ(threat, 200, "Dumb 强度项: 威胁 = 200");

    // 建筑载员修正: +载员×1000
    TechnoClass building;
    building.type = &targetType;
    building.whatAmI = kWhatAmIBuilding;
    building.strength = 100;
    building.occupants = 3;
    int bthreat = 0;
    CHECK(CanAutoTargetObject(&me, &building, rules, &bthreat), "建筑目标可攻击");
    CHECK_EQ(bthreat, 3200, "建筑威胁 = 200 + 3×1000");
}

int main() {
    test_threat_coefficients();
    test_calculate_threat();
    test_can_auto_target();
    printf("\n%s (%d 失败)\n", s_failures == 0 ? "ALL PASS" : "SOME FAILED", s_failures);
    return s_failures == 0 ? 0 : 1;
}
