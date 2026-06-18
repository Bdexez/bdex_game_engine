#include <bdex/core/Config.hpp>

#include <gtest/gtest.h>

using namespace bdex;

TEST(Config, ParsesSectionsAndTypes) {
    const auto config = Config::parse(R"(
# top-level comment
name = demo

[window]
title = My Game
width = 1280
scale = 1.5
fullscreen = yes
; another comment
[engine]
modules = window_glfw, rhi_vulkan ,, renderer_forward
)");
    ASSERT_TRUE(config.has_value()) << config.error().message;

    EXPECT_EQ(config->getString("name"), "demo");
    EXPECT_EQ(config->getString("window.title"), "My Game");
    EXPECT_EQ(config->getInt("window.width"), 1280);
    EXPECT_DOUBLE_EQ(config->getFloat("window.scale"), 1.5);
    EXPECT_TRUE(config->getBool("window.fullscreen"));
    EXPECT_EQ(config->getList("engine.modules"),
              (std::vector<std::string>{"window_glfw", "rhi_vulkan", "renderer_forward"}));
}

TEST(Config, FallbacksApplyToMissingOrMalformedValues) {
    Config config;
    config.set("a.number", "12abc");
    EXPECT_EQ(config.getInt("a.number", 7), 7);
    EXPECT_EQ(config.getInt("a.missing", 9), 9);
    EXPECT_EQ(config.getString("a.missing", "x"), "x");
    EXPECT_FALSE(config.getBool("a.missing"));
    EXPECT_TRUE(config.getList("a.missing").empty());
}

TEST(Config, DefaultsMergeAndOverrides) {
    Config config;
    config.set("window.width", "800");
    config.setDefault("window.width", "1");
    config.setDefault("window.height", "600");
    EXPECT_EQ(config.getInt("window.width"), 800);
    EXPECT_EQ(config.getInt("window.height"), 600);

    Config overrides;
    overrides.set("window.height", "720");
    config.merge(overrides);
    EXPECT_EQ(config.getInt("window.height"), 720);
}

TEST(Config, ReportsSyntaxErrorsWithLineNumbers) {
    const auto missingEquals = Config::parse("[a]\nnot a pair\n");
    ASSERT_FALSE(missingEquals.has_value());
    EXPECT_NE(missingEquals.error().message.find("line 2"), std::string::npos);

    EXPECT_FALSE(Config::parse("[broken\n").has_value());
    EXPECT_FALSE(Config::parse("= value\n").has_value());
}
