// Verifies process execution captures output and passes arguments literally.
#include "kdiag/exec.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

TEST(RealCommandRunnerTest, CapturesOutputAndExitStatus) {
    const CommandResult result = RealCommandRunner().Run({"printf", "%s", "hello"});

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.stdout_text, "hello");
}

TEST(RealCommandRunnerTest, DoesNotInterpretShellMetacharacters) {
    const std::string literal = "safe; printf injected";
    const CommandResult result = RealCommandRunner().Run({"printf", "%s", literal});

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.stdout_text, literal);
}

TEST(RealCommandRunnerTest, EmptyArgumentListFailsCleanly) {
    const CommandResult result = RealCommandRunner().Run({});

    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.stdout_text.find("empty command"), std::string::npos);
}

}  // namespace
}  // namespace kdiag