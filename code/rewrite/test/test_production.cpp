// ============================================================================
// 生产系统 Google Test 用例
// 数值来源: 原版 rules.ini 默认值 (与 code/rewrite/demo.cpp 一致)
// ============================================================================
#include "../production_system.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

using namespace ra2;

namespace {

// 内存版 IniReader (演示外部数据驱动)
class MemoryIni : public IniReader {
public:
    void set(const char* section, const char* key, double value) {
        ini_[section][key] = value;
    }
    double get_double(const char* section, const char* key, double fallback) const override {
        auto it = ini_.find(section);
        if (it == ini_.end()) return fallback;
        auto kit = it->second.find(key);
        if (kit == it->second.end()) return fallback;
        return kit->second;
    }

private:
    std::map<std::string, std::map<std::string, double>> ini_;
};

// 原版 rules.ini [General] 默认值
constexpr double kMinLowPower = 0.5;
constexpr double kMaxLowPower = 1.0;
constexpr double kPenalty = 1.0;
constexpr double kMultipleF = 1.0;
constexpr double kWallBuild = 1.0;
constexpr int kQueueCap = 50;

} // namespace

TEST(Production, LoadFromIni) {
    MemoryIni ini;
    ini.set("General", "MinLowPowerProductionSpeed", 0.3);
    ini.set("General", "MultipleFactory", 0.9);
    auto r = BuildRules::LoadFromIni(ini);
    EXPECT_FLOAT_EQ(r.minLowPowerProductionSpeed, 0.3f);
    EXPECT_FLOAT_EQ(r.multipleFactory, 0.9f);
    EXPECT_FLOAT_EQ(r.maxLowPowerProductionSpeed, 1.0f); // fallback
    EXPECT_FLOAT_EQ(r.lowPowerPenaltyModifier, 1.0f);    // fallback
    EXPECT_EQ(r.buildQueueCap, kQueueCap);
}

TEST(Production, PowerPercentage) {
    HouseClass h;
    h.powerOutput = 100; h.powerDrain = 100;
    EXPECT_DOUBLE_EQ(GetPowerPercentage(&h), 1.0);
    h.powerDrain = 200;
    EXPECT_DOUBLE_EQ(GetPowerPercentage(&h), 0.5);
    h.powerOutput = 0;
    EXPECT_DOUBLE_EQ(GetPowerPercentage(&h), 0.0);
    h.powerOutput = 100; h.powerDrain = 0;
    EXPECT_DOUBLE_EQ(GetPowerPercentage(&h), 1.0); // 需求 0 视为充足
}

TEST(Production, TimeToBuild) {
    const auto rules = BuildRules::LoadFromIni(MemoryIni());

    TechnoTypeClass type;
    type.baseBuildTime = 360;
    type.productWhatAmI = 1; // Unit

    HouseClass house;
    TechnoClass tech;
    tech.type = &type;
    tech.owner = &house;
    tech.rules = &rules;
    house.factoryCount[1] = 1;

    house.powerOutput = 100; house.powerDrain = 100;
    EXPECT_EQ(tech.TimeToBuild(), 360); // 满电
    house.powerDrain = 200;
    EXPECT_EQ(tech.TimeToBuild(), 720); // 半电 2x
    house.powerOutput = 0;
    EXPECT_EQ(tech.TimeToBuild(), 720); // 断电 (下限 0.5)
    house.powerOutput = 90; house.powerDrain = 100;
    EXPECT_EQ(tech.TimeToBuild(), 400); // 90% 电

    // 多工厂
    MemoryIni ini2;
    ini2.set("General", "MultipleFactory", 0.95);
    auto rules2 = BuildRules::LoadFromIni(ini2);
    house.powerOutput = 100; house.powerDrain = 100;
    house.factoryCount[1] = 3;
    tech.objWhatAmI = 1;
    tech.rules = &rules2;
    EXPECT_EQ(tech.TimeToBuild(), 325); // 3 工厂 ×0.95 两次
    tech.rules = &rules;

    // 围墙
    MemoryIni ini3;
    ini3.set("General", "WallBuildSpeedCoefficient", 2.0);
    auto rules3 = BuildRules::LoadFromIni(ini3);
    type.isWall = true;
    tech.objWhatAmI = 6; // Building
    tech.rules = &rules3;
    EXPECT_EQ(tech.TimeToBuild(), 720);
}

TEST(Production, FactoryCore) {
    const auto rules = BuildRules::LoadFromIni(MemoryIni());
    FactoryClass f(rules);
    HouseClass house;
    house.powerOutput = 100; house.powerDrain = 100;

    TechnoTypeClass unit;
    unit.typeId = 1;
    unit.baseBuildTime = 360;
    unit.productWhatAmI = 1;

    EXPECT_EQ(f.GetBuildTimeFrames(), 1); // 无生产物
    f.object = unit.createObject(&house, &rules, false);
    EXPECT_EQ(f.GetBuildTimeFrames(), 6); // 360/54

    f.balance = 2700;
    f.production.value = 0;
    EXPECT_EQ(f.GetCostPerStep(), 50);
    f.production.value = 27;
    EXPECT_EQ(f.GetCostPerStep(), 100);
    f.production.value = kFactorySteps;
    EXPECT_EQ(f.GetCostPerStep(), 2700);

    f.object->type->baseBuildTime = 10;
    EXPECT_EQ(f.GetBuildTimeFrames(), 1); // 钳制下限
    f.object->type->baseBuildTime = 20000;
    EXPECT_EQ(f.GetBuildTimeFrames(), 255); // 钳制上限
    f.object->type->baseBuildTime = 360;

    // Suspend/Unsuspend
    f.production.value = 10;
    EXPECT_TRUE(f.Suspend(false));
    EXPECT_FALSE(f.Suspend(false)); // 重复挂起失败
    EXPECT_TRUE(f.isSuspended);
    EXPECT_EQ(f.production.rate, 0);

    f.owner = &house;
    house.funds = 10000;
    EXPECT_TRUE(f.Unsuspend(false));
    EXPECT_FALSE(f.isSuspended);
    EXPECT_EQ(f.production.rate, 6);

    house.funds = 0;
    f.isSuspended = true;
    EXPECT_FALSE(f.Unsuspend(false)); // 资金不足
    EXPECT_EQ(f.production.rate, 6);

    // CompletedProduction
    f.production.value = kFactorySteps;
    EXPECT_TRUE(f.CompletedProduction());
    EXPECT_EQ(f.object, nullptr);
    EXPECT_TRUE(f.isSuspended);
    EXPECT_EQ(f.production.value, 0);
    EXPECT_TRUE(f.HasProgressChanged());
    EXPECT_FALSE(f.HasProgressChanged());

    delete f.object;
    f.object = nullptr;
}

TEST(Production, Abandon) {
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
    f.balance = 600;
    EXPECT_TRUE(f.AbandonProduction());
    EXPECT_EQ(house.funds, 1400); // 退款 1000-600
    EXPECT_EQ(f.balance, 0);
    EXPECT_EQ(f.object, nullptr);
}

TEST(Production, Queue) {
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

    EXPECT_TRUE(f.DemandProduction(&unit, &house, false)); // 空闲立即生产
    EXPECT_NE(f.object, nullptr);
    EXPECT_EQ(f.queue.size(), 0);

    EXPECT_TRUE(f.DemandProduction(&tank, &house, false)); // 入队
    EXPECT_EQ(f.queue.size(), 1);
    EXPECT_TRUE(f.IsQueued(&tank));
    EXPECT_FALSE(f.IsQueued(&unit)); // 生产中不算
    EXPECT_EQ(f.CountTotal(&tank), 1);
    EXPECT_EQ(f.CountTotal(&unit), 1);
    EXPECT_FALSE(f.DemandProduction(&tank, &house, false)); // 重复拒绝

    f.production.value = 54;
    f.CompletedProduction();
    f.StartProduction();
    EXPECT_NE(f.object, nullptr);
    EXPECT_EQ(f.queue.size(), 0);

    EXPECT_TRUE(f.DemandProduction(&unit, &house, false));
    EXPECT_TRUE(f.RemoveOneFromQueue(&unit));
    EXPECT_EQ(f.queue.size(), 0);
    EXPECT_FALSE(f.RemoveOneFromQueue(&unit));

    delete f.object;
    f.object = nullptr;
}
