#include "TestHarness.h"

int main()
{
    using namespace sonarc::test;

    int failedTests = 0;
    for (const auto& test : registry())
    {
        const int before = failureCount();
        test.body();
        const bool passed = failureCount() == before;
        failedTests += passed ? 0 : 1;
        std::printf("[%s] %s\n", passed ? " OK " : "FAIL", test.name);
    }

    std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
