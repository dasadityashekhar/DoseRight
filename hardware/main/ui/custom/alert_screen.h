#ifndef ALERT_SCREEN_H
#define ALERT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

void alert_screen_init(void);
void alert_screen_show(const char *name, const char *time_str, const char *dose);
void alert_screen_set_on_pick(void (*cb)(void));
void alert_screen_set_on_skip(void (*cb)(void));

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
