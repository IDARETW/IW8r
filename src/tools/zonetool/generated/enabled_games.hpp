#pragma once

// IW8 Radiant builds only IW8 and the IW3 map converter (src/tools/zonetool/UPSTREAM.txt).
#define ZONETOOL_GAMES(x) x(iw3) x(iw8)
#define ZONETOOL_CONVERTERS(x) x(iw3, iw8)
