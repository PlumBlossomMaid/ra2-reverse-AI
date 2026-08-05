// ============================================================================
// RA2 生产系统算法测试 —— 数值取自原版 rules.ini 默认值, 硬编码于此
// 仅用于验证算法逻辑; 实际运行时数值由宿主通过 BuildRules 注入
// 编译: g++ -std=c++17 -I. production_system.cpp demo.cpp -o demo && ./demo
// ============================================================================
#include "production_system.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace ra2;

// ---------------------------------------------------------------------------
// 内存版 IniReader (演示外部数据驱动)
// ---------------------------------------------------------------------------
class MemoryIni : public IniReader {
public:
    void set(const char* section, const char* key, double value) {
        ini[section][key] = value;
    }
    double get_double(const char* section, const char* key, double fallback) const override {
        auto it = ini.find(section);
        if (it == ini.end()) return fallback;
        auto kit = it->second.find(key);
        if (kit == it->second.end()) return fallback;
        return kit->second;
    }

private:
    std::map<std::string, std::map<std::string, double>> ini;
};

// ---------------------------------------------------------------------------
// 断言工具
// ---------------------------------------------------------------------------
static int s_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) { printf("PASS  %s\n", msg); }                               \
        else { printf("FAIL  %s\n", msg); s_failures++; }                      \
    } while (0)

#define CHECK_EQ(a, b, msg)                                                    \
    do {                                                                       \
        int va = (a), vb = (b);                                                \
        if (va == vb) { printf("PASS  %s (%d)\n", msg, va); }                  \
        else { printf("FAIL  %s got=%d want=%d\n", msg, va, vb); s_failures++; } \
    } while (0)

// 原版 rules.ini [General] 默认值 (社区公认)
static constexpr double kMinLowPower = 0.5;
static constexpr double kMaxLowPower = 1.0;
static constexpr double kPenalty     = 1.0;
static constexpr double kMultipleF   = 1.0;
static constexpr double kWallBuild   = 1.0;
static constexpr int    kQueueCap    = 50;

// ---------------------------------------------------------------------------
// 用例
// ---------------------------------------------------------------------------
static void test_load_from_ini() {
    printf("== BuildRules::LoadFromIni ==\n");
    MemoryIni ini;
    ini.set("General", "MinLowPowerProductionSpeed", 0.3);
    ini.set("General", "MultipleFactory", 0.9);
    auto r = BuildRules::LoadFromIni(ini);  // 未设置的项走 fallback
    CHECK_EQ((int)(r.minLowPowerProductionSpeed * 100), 30, "读到 INI 值 0.3");
    CHECK_EQ((int)(r.multipleFactory * 100), 90, "读到 INI 值 0.9");
    CHECK_EQ((int)(r.maxLowPowerProductionSpeed * 100), 100, "fallback 1.0");
    CHECK_EQ((int)(r.lowPowerPenaltyModifier * 100), 100, "fallback 1.0");
    CHECK_EQ(r.buildQueueCap, kQueueCap, "fallback queue cap 50");
}

static void test_power_percentage() {
    printf("== GetPowerPercentage ==\n");
    HouseClass h;
    h.powerOutput = 100; h.powerDrain = 100;
    CHECK_EQ((int)(GetPowerPercentage(&h) * 100), 100, "满电 1.0");
    h.powerDrain = 200;
    CHECK_EQ((int)(GetPowerPercentage(&h) * 100), 50, "半电 0.5");
    h.powerOutput = 0;
    CHECK_EQ((int)(GetPowerPercentage(&h) * 100), 0, "断电 0.0");
    h.powerOutput = 100; h.powerDrain = 0;
    CHECK_EQ((int)(GetPowerPercentage(&h) * 100), 100, "需求为 0 视为充足");
}

static void test_time_to_build() {
    printf("== TimeToBuild (base=360, 难度 1.0, speed 1.0) ==\n");
    const auto rules = BuildRules::LoadFromIni(MemoryIni());

    TechnoTypeClass type;
    type.baseBuildTime = 360;
    type.productWhatAmI = 1;  // Unit

    HouseClass house;
    TechnoClass tech;
    tech.type = &type;
    tech.owner = &house;
    tech.rules = &rules;
    house.factoryCount[1] = 1;

    // 满电
    house.powerOutput = 100; house.powerDrain = 100;
    CHECK_EQ(tech.TimeToBuild(), 360, "满电 360 帧");
    // 半电
    house.powerDrain = 200;
    CHECK_EQ(tech.TimeToBuild(), 720, "半电 720 帧 (2x)");
    // 断电
    house.powerOutput = 0;
    CHECK_EQ(tech.TimeToBuild(), 720, "断电 720 帧 (2x, 下限 0.5)");
    // 90% 电
    house.powerOutput = 90; house.powerDrain = 100;
    CHECK_EQ(tech.TimeToBuild(), 400, "90% 电 400 帧");

    // 多工厂: MultipleFactory=0.95, 3 工厂 → 360*0.95=342 → 342*0.95=325
    MemoryIni ini2;
    ini2.set("General", "MultipleFactory", 0.95);
    auto rules2 = BuildRules::LoadFromIni(ini2);
    house.powerOutput = 100; house.powerDrain = 100;
    house.factoryCount[1] = 3;
    tech.objWhatAmI = 1;  // Unit
    tech.rules = &rules2;
    CHECK_EQ(tech.TimeToBuild(), 325, "3 工厂 ×0.95 两次 → 325");
    tech.rules = &rules;

    // 围墙: WallBuildSpeedCoefficient=2.0
    MemoryIni ini3;
    ini3.set("General", "WallBuildSpeedCoefficient", 2.0);
    auto rules3 = BuildRules::LoadFromIni(ini3);
    type.isWall = true;
    tech.objWhatAmI = 6;  // Building
    tech.rules = &rules3;
    CHECK_EQ(tech.TimeToBuild(), 720, "墙 360×2.0 → 720");
    type.isWall = false;
    tech.rules = &rules;
}

static void test_factory_core() {
    printf("== FactoryClass 核心 ==\n");
    const auto rules = BuildRules::LoadFromIni(MemoryIni());
    FactoryClass f(rules);
    HouseClass house;
    house.powerOutput = 100; house.powerDrain = 100;

    TechnoTypeClass unit;
    unit.typeId = 1;  // UnitType
    unit.baseBuildTime = 360;
    unit.productWhatAmI = 1;

    // GetBuildTimeFrames: 360/54 = 6
    CHECK_EQ(f.GetBuildTimeFrames(), 1, "无生产物时 frames=1");
    f.object = unit.createObject(&house, &rules, false);
    CHECK_EQ(f.GetBuildTimeFrames(), 6, "360 帧 → 每步 6 帧");

    // GetCostPerStep: balance=2700, 54 步 → 每步 50
    f.balance = 2700;
    f.production.value = 0;
    CHECK_EQ(f.GetCostPerStep(), 50, "2700/54=50");
    f.production.value = 27;
    CHECK_EQ(f.GetCostPerStep(), 100, "2700/27=100");
    f.production.value = kFactorySteps;
    CHECK_EQ(f.GetCostPerStep(), 2700, "完成时返回剩余 balance");

    // 钳制: 极端时间
    f.object->type->baseBuildTime = 10;
    CHECK_EQ(f.GetBuildTimeFrames(), 1, "10 帧 → 钳制下限 1");
    f.object->type->baseBuildTime = 20000;
    CHECK_EQ(f.GetBuildTimeFrames(), 255, "20000 帧 → 钳制上限 255");
    f.object->type->baseBuildTime = 360;

    // Suspend / Unsuspend
    f.production.value = 10;
    f.isSuspended = false;
    CHECK(f.Suspend(false), "Suspend 成功");
    CHECK(!f.Suspend(false), "重复 Suspend 失败");
    CHECK(f.isSuspended, "挂起标志置位");
    CHECK_EQ(f.production.rate, 0, "挂起时 Rate 清零");

    // 资金充足 → 恢复
    f.owner = &house;
    house.funds = 10000;
    CHECK(f.Unsuspend(false), "资金充足恢复成功");
    CHECK(!f.isSuspended, "恢复后解除挂起");
    CHECK_EQ(f.production.rate, 6, "恢复后重算 Rate=6");

    // 资金不足 → 不恢复 (原版 quirk: 返回 false, isSuspended 已清)
    house.funds = 0;
    f.isSuspended = true;
    CHECK(!f.Unsuspend(false), "资金不足拒绝恢复");
    CHECK_EQ(f.production.rate, 6, "Rate 保持重算值");

    // CompletedProduction
    f.production.value = kFactorySteps;
    CHECK(f.CompletedProduction(), "进度 54 完成");
    CHECK(f.object == nullptr, "完成后清除生产物");
    CHECK(f.isSuspended, "完成后挂起等待部署");
    CHECK_EQ(f.production.value, 0, "完成后进度清零");
    CHECK(f.HasProgressChanged(), "HasProgressChanged 报告变化");
    CHECK(!f.HasProgressChanged(), "二次读取清零");

    delete f.object;
    f.object = nullptr;
}

static void test_abandon() {
    printf("== AbandonProduction ==\n");
    const auto rules = BuildRules::LoadFromIni(MemoryIni());
    FactoryClass f(rules);
    HouseClass house;
    house.funds = 1000;

    TechnoTypeClass unit;
    unit.typeId = 1;
    unit.cost = 1000;
    unit.productWhatAmI = 1;

    f.owner = &house;
    f.object = unit.createObject(&house, &rules, false);
    f.balance = 600;  // 已扣 600, 剩余欠款 600? 见下
    // 退款 = actualCost - balance; balance 是剩余欠款
    // 设 balance=600 表示还欠 600, 已付 400 → 退款 400
    CHECK(f.AbandonProduction(), "放弃生产");
    CHECK_EQ(house.funds, 1400, "退款 = 1000 - 600 = 400");
    CHECK_EQ(f.balance, 0, "放弃后 balance 清零");
    CHECK(f.object == nullptr, "放弃后清除生产物");
}

static void test_queue() {
    printf("== 队列 ==\n");
    const auto rules = BuildRules::LoadFromIni(MemoryIni());
    FactoryClass f(rules);
    HouseClass house;
    house.powerOutput = 100; house.powerDrain = 100;
    house.factoryCount[1] = 1;

    TechnoTypeClass unit;
    unit.typeId = 1;
    unit.baseBuildTime = 360;
    unit.productWhatAmI = 1;

    TechnoTypeClass tank;
    tank.typeId = 1;
    tank.baseBuildTime = 600;
    tank.productWhatAmI = 1;

    // 工厂空闲 → 立即生产
    CHECK(f.DemandProduction(&unit, &house, false), "空闲时立即生产");
    CHECK(f.object != nullptr, "立即生产建立 object");
    CHECK(f.queue.size() == 0, "未排队");

    // 正在生产 → 入队
    CHECK(f.DemandProduction(&tank, &house, false), "生产中人队");
    CHECK_EQ(f.queue.size(), 1, "队列 1 项");
    CHECK(f.IsQueued(&tank), "IsQueued 命中");
    CHECK(!f.IsQueued(&unit), "IsQueued 未命中(生产中不算)");
    CHECK_EQ(f.CountTotal(&tank), 1, "CountTotal 队列 1");
    CHECK_EQ(f.CountTotal(&unit), 1, "CountTotal 生产中 1");

    // 重复排队 → 拒绝
    CHECK(!f.DemandProduction(&tank, &house, false), "重复排队被拒绝");

    // 队首出队开始生产
    f.production.value = 54;
    f.CompletedProduction();
    f.StartProduction();
    CHECK(f.object != nullptr, "StartProduction 建立 object");
    CHECK_EQ(f.queue.size(), 0, "出队后队列空");

    // RemoveOneFromQueue
    CHECK(f.DemandProduction(&unit, &house, false), "再次排队");
    CHECK(f.RemoveOneFromQueue(&unit), "RemoveOneFromQueue 成功");
    CHECK_EQ(f.queue.size(), 0, "移除后队列空");
    CHECK(!f.RemoveOneFromQueue(&unit), "移除不存在项失败");

    delete f.object;
    f.object = nullptr;
}

int main() {
    test_load_from_ini();
    test_power_percentage();
    test_time_to_build();
    test_factory_core();
    test_abandon();
    test_queue();
    printf("\n%s (%d 失败)\n", s_failures == 0 ? "ALL PASS" : "SOME FAILED", s_failures);
    return s_failures == 0 ? 0 : 1;
}
