// ============================================================================
// 威胁评估系统 Google Test 用例
// 数值来源: 原版 rulesmd.ini [General] (Official Rules of Engagement, YR)
// ============================================================================
#include "../threat_system.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace ra2;

namespace {

// 原版 rulesmd.ini [General] 威胁系数
ThreatRules make_rules() {
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

ThreatRules make_rules_with_own() {
    ThreatRules r = make_rules();
    r.defaultCoeff.myEffectiveness = 200.0;
    r.defaultCoeff.targetEffectiveness = -200.0;
    r.defaultCoeff.targetSpecialThreat = 200.0;
    r.defaultCoeff.targetStrength = -200.0;
    r.defaultCoeff.targetDistance = -10.0;
    return r;
}

TechnoTypeClass make_type(int threatPosed, int armor, int strength,
                          int houseId, double specialThreat = 0.0) {
    TechnoTypeClass t;
    t.threatPosed = threatPosed;
    t.armor = armor;
    t.strength = strength;
    t.houseId = houseId;
    t.specialThreatValue = specialThreat;
    return t;
}

void make_combatants(TechnoClass& me, TechnoClass& target, TechnoTypeClass& myType,
                     TechnoTypeClass& targetType, WarheadType& myWH,
                     WarheadType& tgtWH, WeaponType& myWeapon, WeaponType& tgtWeapon) {
    myType = make_type(0, 0, 100, 1);
    targetType = make_type(0, 0, 100, 2);

    myWH.verses[0] = 100.0;
    myWeapon.warhead = &myWH;
    myWeapon.range = 256;

    tgtWH.verses[0] = 50.0;
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

} // namespace

TEST(Threat, Coefficients) {
    auto rules = make_rules_with_own();
    TechnoClass me, target;
    TechnoTypeClass myType, targetType;
    WarheadType myWH, tgtWH;
    WeaponType myWeapon, tgtWeapon;
    make_combatants(me, target, myType, targetType, myWH, tgtWH, myWeapon, tgtWeapon);
    me.type->useOwnCoefficients = true;
    me.type->coeff = rules.defaultCoeff;

    // 200×100 − 200×50 + 0 + 400 − 200 = 10200
    CoordStruct loc{};
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 10200);

    // 距离 512, 射程 1 格 → 超出 511 × (−10) → 5090
    loc.x = 512;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 5090);
    loc = {};

    // 同阵营无加成 → 9800
    target.type->houseId = 1;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 9800);
    target.type->houseId = 2;

    // 特殊威胁 500 → +100000 → 110200
    targetType.specialThreatValue = 500.0;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 110200);
    targetType.specialThreatValue = 0.0;

    // 类型覆盖 MyEffectiveness=400 → 30200
    me.type->coeff.myEffectiveness = 400.0;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 30200);
    me.type->coeff = rules.defaultCoeff;

    // Dumb 系数 (无武器): 200×50 + 400 + 200 = 10600
    me.primaryWeapon = nullptr;
    me.type->useOwnCoefficients = false;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), 10600);

    // 目标瞄准我 → 反击贡献取负: −200×50 + 400 + 200 = −9400
    target.target = &me;
    EXPECT_EQ((int)std::nearbyint(ThreatCoefficients(&me, &target, rules, &loc)), -9400);
}

TEST(Threat, CalculateThreat) {
    auto rules = make_rules();

    TechnoTypeClass unitType = make_type(25, 0, 100, 1);
    TechnoClass unit;
    unit.type = &unitType;
    unit.whatAmI = kWhatAmIUnit;
    EXPECT_EQ(CalculateThreat(&unit, rules), 25); // 单位 = Type.ThreatPosed

    TechnoTypeClass bldType = make_type(10, 0, 100, 1);
    TechnoClass building;
    building.type = &bldType;
    building.whatAmI = kWhatAmIBuilding;
    building.occupants = 4;
    EXPECT_EQ(CalculateThreat(&building, rules), 40); // 载员×10

    building.occupants = 0;
    TechnoClass garrison;
    garrison.type = &unitType;
    building.garrison = &garrison;
    EXPECT_EQ(CalculateThreat(&building, rules), 25); // 驻防

    building.garrison = nullptr;
    EXPECT_EQ(CalculateThreat(&building, rules), 10); // 空建筑
}

TEST(Threat, CanAutoTarget) {
    auto rules = make_rules();

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
    EXPECT_TRUE(CanAutoTargetObject(&me, &target, rules, &threat));
    EXPECT_GE(threat, 1);
    EXPECT_EQ(threat, 200); // Dumb 强度项 200×1.0

    TechnoClass building;
    building.type = &targetType;
    building.whatAmI = kWhatAmIBuilding;
    building.strength = 100;
    building.occupants = 3;
    int bthreat = 0;
    EXPECT_TRUE(CanAutoTargetObject(&me, &building, rules, &bthreat));
    EXPECT_EQ(bthreat, 3200); // 200 + 3×1000
}
