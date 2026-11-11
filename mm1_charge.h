#pragma once
#include "cyc_host_extras.h"
#include "cyc_mm4_charge_data.h"
int mm1_charge_available(void);
int mm1_mm4_load(const char *feature);
int mm1_charge_flashing(void);
int mm1_charge_kind(int slot);
int mm1_charge_sprite(int slot);
const CycMm4ChargeData *mm1_charge_data(void);
void mm1_charge_dispatch(void);
void mm1_charge_cancel(void);
void mm1_charge_weapon_changed(void);
void mm1_charge_stage_enter(void);
const CycHostExtras *mm1_charge_extras(void);
