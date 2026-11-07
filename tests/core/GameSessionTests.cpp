#include "talvisota/core/GameSession.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
int g_failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
}

void TestNormalLifecycle()
{
    talvisota::core::GameSession session;
    Check(session.Phase() == talvisota::core::GamePhase::Cold, "starts cold");
    Check(!session.BeginLoading(), "cannot load before initialization");
    Check(session.BeginInitialization(), "begin initialization");
    Check(session.FinishInitialization(), "finish initialization");
    Check(session.BeginLoading(), "begin loading");
    Check(session.FinishLoading(), "enter gameplay");
    Check(session.Pause(), "pause gameplay");
    Check(session.Resume(), "resume gameplay");
    Check(session.ReturnToFrontend(), "return to frontend");
    Check(session.BeginShutdown(), "begin shutdown");
    Check(session.FinishShutdown(), "finish shutdown");
    Check(!session.BeginInitialization(), "stopped session cannot restart");
}

void TestLoadingCancellation()
{
    talvisota::core::GameSession session;
    Check(session.BeginInitialization(), "cancel path initializes");
    Check(session.FinishInitialization(), "cancel path reaches frontend");
    Check(session.BeginLoading(), "cancel path begins loading");
    Check(session.CancelLoading(), "loading can be cancelled");
    Check(session.Phase() == talvisota::core::GamePhase::Frontend,
          "cancel returns to frontend");
}

void TestFailureLifecycle()
{
    talvisota::core::GameSession session;
    Check(session.BeginInitialization(), "failure path initializes");
    Check(session.Fail("renderer initialization failed"), "failure is accepted");
    Check(session.Phase() == talvisota::core::GamePhase::Failed, "phase becomes failed");
    Check(session.FailureReason() == "renderer initialization failed",
          "failure reason is retained");
    Check(!session.Fail("second failure"), "failure cannot be overwritten");
    Check(session.BeginShutdown(), "failed session can shut down");
    Check(session.FinishShutdown(), "failed session can finish shutdown");
}

void TestInvalidFailureReasons()
{
    talvisota::core::GameSession session;
    Check(!session.Fail("failure before startup"), "cannot fail before startup");
    Check(session.BeginInitialization(), "empty-reason path initializes");
    Check(!session.Fail(""), "empty failure reason is rejected");
    Check(session.Phase() == talvisota::core::GamePhase::Initializing,
          "rejected failure does not change phase");
}
}

int main()
{
    TestNormalLifecycle();
    TestLoadingCancellation();
    TestFailureLifecycle();
    TestInvalidFailureReasons();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Talvisota core tests passed\n";
    return EXIT_SUCCESS;
}
