#include <gtest/gtest.h>
#include "alarm/alarm_engine.h"

using namespace minitsdb;

TEST(AlarmEngineTest, AddAndEvaluate) {
    AlarmEngine engine;

    AlarmRule rule;
    rule.name = "high_temp";
    rule.tag_name = "BOILER-001";
    rule.condition = "value > 1400.0";
    rule.actions = {"log"};

    ASSERT_TRUE(engine.AddRule(rule));

    int alarm_count = 0;
    engine.SetOnAlarm([&](const AlarmEvent&) { alarm_count++; });

    DataPoint dp;
    dp.ts = 1000;

    // 低于阈值，不触发
    dp.value = 1000.0;
    engine.Evaluate("BOILER-001", dp);
    EXPECT_EQ(alarm_count, 0);

    // 高于阈值，触发
    dp.value = 1500.0;
    engine.Evaluate("BOILER-001", dp);
    EXPECT_EQ(alarm_count, 1);

    // 不同 tag，不触发
    dp.value = 1500.0;
    engine.Evaluate("BOILER-002", dp);
    EXPECT_EQ(alarm_count, 1);
}

TEST(AlarmEngineTest, MultipleOperators) {
    auto test_rule = [](const std::string& cond, double val) {
        AlarmEngine engine;
        AlarmRule rule;
        rule.name = "test";
        rule.tag_name = "T";
        rule.condition = cond;
        engine.AddRule(rule);

        int count = 0;
        engine.SetOnAlarm([&](const AlarmEvent&) { count++; });

        DataPoint dp;
        dp.ts = 1;
        dp.value = val;
        engine.Evaluate("T", dp);
        return count;
    };

    EXPECT_EQ(test_rule("value > 10", 11), 1);
    EXPECT_EQ(test_rule("value > 10", 10), 0);
    EXPECT_EQ(test_rule("value >= 10", 10), 1);
    EXPECT_EQ(test_rule("value < 10", 9), 1);
    EXPECT_EQ(test_rule("value < 10", 10), 0);
    EXPECT_EQ(test_rule("value <= 10", 10), 1);
    EXPECT_EQ(test_rule("value == 10", 10), 1);
    EXPECT_EQ(test_rule("value == 10", 11), 0);
    EXPECT_EQ(test_rule("value != 10", 11), 1);
}

TEST(AlarmEngineTest, DuplicateRule) {
    AlarmEngine engine;
    AlarmRule r1, r2;
    r1.name = "same";
    r1.condition = "v > 1";
    r2.name = "same";
    r2.condition = "v > 2";

    EXPECT_TRUE(engine.AddRule(r1));
    EXPECT_FALSE(engine.AddRule(r2));  // 同名应失败
}

TEST(AlarmEngineTest, QueryEvents) {
    AlarmEngine engine;
    AlarmRule rule;
    rule.name = "alarm1";
    rule.tag_name = "T";
    rule.condition = "v > 5";
    engine.AddRule(rule);

    DataPoint dp;
    dp.value = 10.0;

    dp.ts = 100;
    engine.Evaluate("T", dp);
    dp.ts = 200;
    engine.Evaluate("T", dp);

    auto events = engine.QueryEvents("T", 50, 150);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].ts, 100);
}
