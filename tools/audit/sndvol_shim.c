/* tools/audit/sndvol_diff.py: the port's positional sound level (src/sfx.h sfx_pos_level) for ctypes */
#include "sfx.h"
int port_pos_level(long dist_cm) { return sfx_pos_level(dist_cm); }
float port_pan(float yaw, float a) { return (float)sfx_pan_engine_ang(yaw, a); }
