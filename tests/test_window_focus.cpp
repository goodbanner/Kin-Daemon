#include "WindowFocus.hpp"
#include <cstdio>
int main() {
    const char* focus = "mFocusedApp=ActivityRecord{x u10 com.game.app/.Main}";
    const char* current = "mCurrentFocus=Window{x u0 com.overlay.app/.Main}";
    struct Case { const char* first; const char* second; const char* want; };
    const Case cases[] = {
        {focus, current, "com.game.app"},
        {current, focus, "com.game.app"},
        {"mFocusedApp=null", current, "com.overlay.app"},
        {current, "", "com.overlay.app"},
        {focus, "", "com.game.app"},
        {"mCurrentFocus=null", "", ""},
        {"mFocusedApp=x u0 com.bad;app/.Main", current, "com.overlay.app"},
        {"mFocusedApp=x u com.bad.app/.Main", "", ""},
        {"mFocusedApp=x u0 com.bad.app", "", ""},
    };
    for (const auto& c : cases) {
        kin::WindowFocus parser;
        parser.consume(c.first);
        parser.consume(c.second);
        if (parser.package() != c.want) {
            std::fprintf(stderr, "expected %s, got %s\n", c.want, parser.package().c_str());
            return 1;
        }
    }
    std::puts("PASS: 9 window focus cases");
}
