#include <gtest/gtest.h>

#include "core/version.h"

using namespace eda;

TEST(SmokeBuild, VersionReturnsString) {
    const char* v = eda::version();
    EXPECT_NE(v, nullptr);
    EXPECT_STRNE(v, "");
}

TEST(SmokeBuild, VersionMatchesExpected) {
    EXPECT_STREQ(eda::version(), "0.0.1");
}
