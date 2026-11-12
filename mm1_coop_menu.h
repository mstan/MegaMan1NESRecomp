#pragma once
#include <stdint.h>
int mm1_coop_menu_active(void);
void mm1_coop_menu_reset(void);
void mm1_coop_menu_cancel(void);
void mm1_coop_menu_refresh(void);
void mm1_coop_menu_input(uint8_t buttons[2]);
int mm1_coop_menu_draw(uint32_t *out,int width,int own);
void mm1_coop_menu_debug(char *out,int capacity);
