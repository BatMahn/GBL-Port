/* shim_msim.c - the port's msim.c with its statics reachable, for tools/audit/simdiff.py */
#include "../../src/msim.c"
float shim_leg_demand(float input, float spd, int player, float gain, int airborne, int autopilot, float prev)
{ return msim_leg_demand(input, spd, player, gain, airborne, autopilot, prev); }
float shim_servo(float cur, float tgt, float T, float ticks) { return drive_servo(cur, tgt, T, ticks); }
float shim_twist_round(float cur, float demand, float T, float ticks) { return msim_twist_round(cur, demand, T, ticks); }
