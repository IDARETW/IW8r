// iw8r live-read <pid>: read-only check of the direct live link (attach, address map, player view). Writes nothing.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "live_direct.h"

int cmdLiveRead(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: iw8r live-read <pid>\n");
        return 2;
    }
    iw8r::live::DirectLink link;
    std::string err;
    if (!link.attach(uint32_t(std::strtoul(argv[2], nullptr, 10)), &err)) {
        std::fprintf(stderr, "attach failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("attached pid %u base 0x%llx %s\n", link.pid(), (unsigned long long)link.base(),
                link.recompiled() ? "recompiled (.rplmap)" : "stock");
    if (auto v = link.readPlayer(&err))
        std::printf("player origin %.2f %.2f %.2f angles %.2f %.2f %.2f\n", v->origin.x, v->origin.y, v->origin.z, v->angles.x,
                    v->angles.y, v->angles.z);
    else
        std::printf("player: %s\n", err.c_str());
    return 0;
}
