#!/usr/bin/env python3
"""Exercise the actual SDL focus-wait functions with a fake Symbian scheduler."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text(encoding="latin-1")
start = source.index("class CWaitForActivate:public CActive")
end = source.index("void UpdateKbdMouse()", start)
focus_methods = source[start:end]

stubs = r"""
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
typedef int TInt;
typedef void TAny;
const int KRequestPending = -1;

static int stops, redraws, arms;
static bool gViewVisible, exitRequested;
static int current_video;
extern "C" int cpymo_s60_exit_is_requested() { return exitRequested; }
void RedrawWindowL(int) { ++redraws; }

class CActive {
protected:
    enum { EPriorityStandard };
    int iStatus;
public:
    CActive(int) {}
    void SetActive() {}
};
class RTimer {
public:
    void CreateLocal() {}
    void Close() {}
    void After(int&, int us) { assert(us == 250000); ++arms; }
    void Cancel() {}
};
struct CActiveScheduler {
    static void Add(CActive*) {}
    static void Stop() { ++stops; }
};
struct User {
    static void ResetInactivityTime() {}
};
"""

cases = r"""
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "exit-before-wait")) {
        exitRequested = true;
        StopSchedulerTimeOut(NULL);
        assert(stops == 1 && !gWaitForActive && redraws == 0);
    } else if (!strcmp(argv[1], "exit-during-wait")) {
        StopSchedulerTimeOut(NULL);
        assert(stops == 0 && gWaitForActive && arms == 1);
        exitRequested = true;
        gWaitForActive->RunL();
        assert(stops == 1 && !gWaitForActive && redraws == 0 && arms == 1);
    } else if (!strcmp(argv[1], "background-resume")) {
        StopSchedulerTimeOut(NULL);
        assert(stops == 0 && gWaitForActive);
        gWaitForActive->RunL();
        assert(stops == 0 && gWaitForActive && arms == 2 && redraws == 0);
        gViewVisible = true;
        gWaitForActive->RunL();
        assert(stops == 1 && !gWaitForActive && redraws == 1);
    } else {
        assert(false);
    }
    std::puts(argv[1]);
}
"""

with tempfile.TemporaryDirectory() as tmp:
    harness = Path(tmp) / "focus_wait.cpp"
    binary = Path(tmp) / "focus_wait"
    harness.write_text(stubs + focus_methods + cases, encoding="latin-1")
    subprocess.run(["c++", "-std=c++98", "-DS60V3", str(harness), "-o", str(binary)], check=True)
    failed = False
    for test in ("background-resume", "exit-before-wait", "exit-during-wait"):
        result = subprocess.run([binary, test], capture_output=True, check=False)
        if result.returncode != 0:
            print(f"FAIL: {test}")
            failed = True
        else:
            print(f"PASS: {test}")
    sys.exit(1 if failed else 0)
