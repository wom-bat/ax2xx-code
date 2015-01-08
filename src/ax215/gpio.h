#ifndef __GPIO_H__
#define __GPIO_H__

#define GPIO_IS_EIM (0x80000000)

enum gpio_dir {
	GPIO_IN = 0,
	GPIO_OUT = 1,
};

int gpio_export(int gpio);
int gpio_unexport(int gpio);
int gpio_set_direction(int gpio, int is_output);
int gpio_set_value(int gpio, int value);
int gpio_get_value(int gpio);


#ifdef NOVENA
int eim_set_direction(int gpio, int is_output);
int eim_set_value(int gpio, int value);
int eim_get_value(int gpio);
#else
static inline int eim_set_direction(int gpio, int is_output) { return 0; }
static inline int eim_set_value(int gpio, int value) { return 0; }
static inline int eim_get_value(int gpio) { return 0; }
#endif
#endif /* __GPIO_H__ */
