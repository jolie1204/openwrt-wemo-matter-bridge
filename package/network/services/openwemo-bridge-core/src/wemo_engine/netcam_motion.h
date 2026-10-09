#ifndef NETCAM_MOTION_H
#define NETCAM_MOTION_H
#include <string.h>
/* Packed snapshot: low bit is last valid occupancy; upper bits are mode.
 * Unknown/legacy availability never invents a valid observation. */
enum netcam_motion_mode { NC_UNKNOWN, NC_IDLE, NC_MOTION, NC_PAUSED, NC_UNAVAILABLE, NC_SETUP, NC_LEGACY };
static inline int netcam_motion_available(int packed) { return packed == (NC_IDLE << 1) || packed == ((NC_MOTION << 1) | 1); }
static inline int netcam_motion_parse(const char *binary, const char *mode, const char *available, int previous)
{
    int last = previous >= 0 && previous <= ((NC_LEGACY << 1) | 1) ? previous & 1 : 0;
    int bit = binary && (!strcmp(binary,"0") || !strcmp(binary,"1")) ? binary[0]-'0' : -1;
    if (!mode && !available) return bit >= 0 ? (NC_LEGACY << 1) | bit : last;
    if (!mode || !available || bit < 0) return last;
    if (!strcmp(available,"1")) {
        if (!strcmp(mode,"idle") && bit == 0) return NC_IDLE << 1;
        if (!strcmp(mode,"motion") && bit == 1) return (NC_MOTION << 1) | 1;
        return last;
    }
    if (strcmp(available,"0")) return last;
    if (!strcmp(mode,"paused")) return (NC_PAUSED << 1) | last;
    if (!strcmp(mode,"unavailable")) return (NC_UNAVAILABLE << 1) | last;
    if (!strcmp(mode,"setup")) return (NC_SETUP << 1) | last;
    return last;
}
/* Shared push/cache freshness policy. Unchanged GENA events are not promised;
 * controller background GetMotionStatus refresh is the liveness source. */
static inline int netcam_motion_normalize(int packed, int online, long long seen, long long now)
{
    if (packed < 0 || packed > ((NC_LEGACY << 1) | 1)) return 0;
    if (packed == 3 || packed == 4) return packed & 1;
    return online && seen > 0 && now >= seen && now - seen <= 600 ? packed : packed & 1;
}
#endif
