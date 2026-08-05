// ============================================================================
// RA2 威胁评估系统算法测试
// 注意: 原版 rules.ini 的具体系数值未确认 (游戏内建默认 0, 运行时加载),
// 本测试采用相对断言验证公式结构——不依赖绝对值。
// 编译: cl /std:c++17 /utf-8 /W4 threat_system.cpp demo_threat.cpp /Fe:demo_threat.exe
// ============================================================================
#include "threat_system.h"

#include <cstdio>

using namespace ra2;

static int s_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) { printf("PASS  %s\n", msg); }                               \
        else { printf("FAIL  %s\n", msg); s_failures++; }                      \
    } while (0)

#define CHECK_GT(a, b, msg)                                                    \
    do {                                                                       \
        double va = (a), vb = (b);                                             \
        if (va > vb) { printf("PASS  %s (%.3f > %.3f)\n", msg, va, vb); }      \
        else { printf("FAIL  %s (%.3f <= %.3f)\n", msg, va, vb); s_failures++; } \
    } while (0)

// 测试用规则 (硬编码: 结构验证值, 非原版数值)
static ThreatRules make_rules() {
    ThreatRules r;
    r.dumbDefault.myEffectiveness = 1.0;
    r.dumbDefault.targetEffectiveness = 1.0;
    r.dumbDefault.targetSpecialThreat = 1.0;
    r.dumbDefault.targetStrength = 1.0;
    r.dumbDefault.targetDistance = -0.1; // 距离惩罚系数为负 → 越远威胁越低
    r.enemyHouseThreatBonus = 10.0;
    r.threatPerOccupant = 50;
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

static void test_threat_coefficients() {
    printf("== ThreatCoefficients ==\n");
    auto rules = make_rules();

    // 敌我双方类型 (不同阵营)
    TechnoTypeClass myType = make_type(0, 0, 100, 1);
    TechnoTypeClass targetType = make_type(0, 0, 100, 2);

    // 双方武器 (Verses: 对 armor 0 的伤害倍率)
    WarheadType myWarhead;
    myWarhead.verses[0] = 100.0;
    WeaponType myWeapon;
    myWeapon.warhead = &myWarhead;
    myWeapon.range = 256; // 256/256 = 1 格

    WarheadType tgtWarhead;
    tgtWarhead.verses[0] = 50.0;
    WeaponType tgtWeapon;
    tgtWeapon.warhead = &tgtWarhead;
    tgtWeapon.range = 256;

    TechnoClass me;
    me.type = &myType;
    me.primaryWeapon = &myWeapon;
    me.strength = 100;

    TechnoClass target;
    target.type = &targetType;
    target.primaryWeapon = &tgtWeapon;
    target.strength = 100;
    target.whatAmI = kWhatAmIUnit;

    // 场景A: 同格 (距离0), 无特殊威胁, 异阵营
    CoordStruct loc{};
    double threat = ThreatCoefficients(&me, &target, rules, &loc);
    // 预期: 100(我打它) + 50(它打我) + 0(特殊) + 10(异阵营) + 1(强度) + 0(距离)
    CHECK_GT(threat, 150.0, "异阵营+双方武器: 威胁 > 150");

    // 场景B: 距离惩罚 (距离 512, 射程 1格=256 → 超出 1 格)
    loc.x = 512;
    double threatFar = ThreatCoefficients(&me, &target, rules, &loc);
    CHECK_GT(threat, threatFar, "距离越远威胁越低 (距离惩罚)");

    // 场景C: 同阵营 → 无加成
    TechnoClass ally;
    ally.type = &targetType;
    ally.type->houseId = 1; // 与我同阵营
    ally.primaryWeapon = &tgtWeapon;
    ally.strength = 100;
    ally.whatAmI = kWhatAmIUnit;
    CoordStruct loc0{};
    double threatAlly = ThreatCoefficients(&me, &ally, rules, &loc0);
    CHECK_GT(threat, threatAlly, "异阵营有加成, 同阵营无");

    // 场景D: 特殊威胁值加成
    TechnoTypeClass specialType = make_type(0, 0, 100, 2, 500.0);
    TechnoClass special;
    special.type = &specialType;
    special.primaryWeapon = &tgtWeapon;
    special.strength = 100;
    special.whatAmI = kWhatAmIUnit;
    double threatSpecial = ThreatCoefficients(&me, &special, rules, &loc0);
    CHECK_GT(threatSpecial, threat, "特殊威胁值加成 (SpecialThreatValue)");

    // 场景E: 类型覆盖系数 (useOwnCoefficients)
    myType.useOwnCoefficients = true;
    myType.coeff = rules.dumbDefault;
    myType.coeff.myEffectiveness = 2.0; // 我打目标权重翻倍
    double threatOwn = ThreatCoefficients(&me, &target, rules, &loc0);
    CHECK_GT(threatOwn, threat, "类型覆盖系数: MyEffectiveness 权重生效");
    myType.useOwnCoefficients = false;
}

static void test_calculate_threat() {
    printf("== CalculateThreat (珍宝函数) ==\n");
    auto rules = make_rules();

    // 单位: 威胁 = Type->ThreatPosed
    TechnoTypeClass unitType = make_type(25, 0, 100, 1);
    TechnoClass unit;
    unit.type = &unitType;
    unit.whatAmI = kWhatAmIUnit;
    CHECK(CalculateThreat(&unit, rules) == 25, "单位威胁 = Type.ThreatPosed");

    // 建筑: 威胁 = 载员数 × ThreatPerOccupant
    TechnoTypeClass bldType = make_type(10, 0, 100, 1);
    TechnoClass building;
    building.type = &bldType;
    building.whatAmI = kWhatAmIBuilding;
    building.occupants = 4;
    CHECK(CalculateThreat(&building, rules) == 200, "建筑威胁 = 载员×ThreatPerOccupant (4×50)");

    // 建筑无载员: 驻防单位类型威胁
    building.occupants = 0;
    TechnoClass garrison;
    garrison.type = &unitType; // ThreatPosed=25
    building.garrison = &garrison;
    CHECK(CalculateThreat(&building, rules) == 25, "驻防建筑威胁 = 驻防单位类型威胁");

    // 建筑空置: 自身类型威胁
    building.garrison = nullptr;
    CHECK(CalculateThreat(&building, rules) == 10, "空建筑威胁 = 自身类型威胁");
}

static void test_can_auto_target() {
    printf("== CanAutoTargetObject ==\n");
    auto rules = make_rules();

    TechnoTypeClass myType = make_type(0, 0, 100, 1);
    TechnoClass me;
    me.type = &myType;
    me.strength = 100;

    TechnoTypeClass targetType = make_type(0, 0, 100, 1); // 同阵营 → 无加成
    TechnoClass target;
    target.type = &targetType;
    target.whatAmI = kWhatAmIUnit;
    target.strength = 100;

    int threat = 0;
    CHECK(CanAutoTargetObject(&me, &target, rules, &threat), "目标可攻击");
    CHECK(threat >= 1, "威胁值下限 = 1");
    CHECK(threat == 1, "无武器同阵营时威胁 = 1 (仅强度项)");

    // 建筑载员修正: 威胁 += 载员×1000
    TechnoClass building;
    building.type = &targetType;
    building.whatAmI = kWhatAmIBuilding;
    building.strength = 100;
    building.occupants = 3;
    int bthreat = 0;
    CHECK(CanAutoTargetObject(&me, &building, rules, &bthreat), "建筑目标可攻击");
    CHECK(bthreat >= 3000, "建筑威胁包含载员×1000 修正 (3×1000)");
}

int main() {
    test_threat_coefficients();
    test_calculate_threat();
    test_can_auto_target();
    printf("\n%s (%d 失败)\n", s_failures == 0 ? "ALL PASS" : "SOME FAILED", s_failures);
    return s_failures == 0 ? 0 : 1;
}
