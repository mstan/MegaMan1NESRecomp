#pragma once
#include <stdint.h>
typedef struct {int coop,independent,roster,wait_both,proto,mode,start,weapons,wide,extended,flicker,lines,charge,slide;} Mm1Settings;
int mm1_net_locked(void);
int mm1_net_simulation_width(int local_width);
void mm1_net_note_option(const char *name);
Mm1Settings mm1_host_settings(void);
int mm1_host_apply_settings(Mm1Settings settings,int local_seat);
int mm1_host_revalidate_donor(int charge,int slide);
int mm1_proto_source_hash(uint8_t out[32]);
