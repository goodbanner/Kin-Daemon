#include "PresenceCache.hpp"
#include <cstdio>
struct Clock { using time_point=std::chrono::steady_clock::time_point; static inline time_point t{}; static time_point now(){return t;} static void advance(int n){t+=std::chrono::milliseconds(n);} };
int main(){
    kin::PresenceCache<Clock> c;
    int calls=0;
    auto p=[&](const std::string&){++calls; return true;};
    if(!c.alive("game",2000,p)||calls!=1)return 1;
    Clock::advance(1999);
    if(!c.alive("game",2000,p)||calls!=1)return 2;
    Clock::advance(1);
    if(!c.alive("game",2000,p)||calls!=2)return 3;
    if(!c.alive("other",2000,p)||calls!=3)return 4;

    kin::PresenceCache<Clock> negative;
    auto miss=[&](const std::string&){++calls; return false;};
    if(negative.alive("gone",2000,miss)||calls!=4)return 5;
    Clock::advance(1999);
    if(negative.alive("gone",2000,miss)||calls!=4)return 6;
    Clock::advance(1);
    if(negative.alive("gone",2000,miss)||calls!=5)return 7;
    std::puts("PASS presence cache: positive/negative TTL and key changes");
}
