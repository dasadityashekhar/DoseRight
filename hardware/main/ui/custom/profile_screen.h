#ifndef PROFILE_SCREEN_H
#define PROFILE_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

void profile_screen_init(void);
void profile_screen_show(void);
void profile_screen_set_on_back(void (*cb)(void));
void profile_screen_preload(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
