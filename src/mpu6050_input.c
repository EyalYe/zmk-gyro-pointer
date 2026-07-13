#define DT_DRV_COMPAT zmk_mpu6050_input

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

struct cfg {
    const struct device *sensor;
    uint16_t poll_ms;
    int32_t mult, div;
    bool swap_xy, invert_x, invert_y;
};

struct data {
    const struct device *dev;
    struct k_work_delayable work;
};

static void poll_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct data *d = CONTAINER_OF(dwork, struct data, work);
    const struct cfg *cfg = d->dev->config;
    struct sensor_value g[3];

    if (sensor_sample_fetch(cfg->sensor) == 0 &&
        sensor_channel_get(cfg->sensor, SENSOR_CHAN_GYRO_XYZ, g) == 0) {

        /* yaw (gz) -> X, pitch (gx or gy) -> Y; tune to mounting */
        int32_t dx = (int32_t)sensor_value_to_milli(&g[2]) * cfg->mult / cfg->div / 1000;
        int32_t dy = (int32_t)sensor_value_to_milli(&g[0]) * cfg->mult / cfg->div / 1000;

        /* pmw3610-style transforms */
        if (cfg->swap_xy) { int32_t t = dx; dx = dy; dy = t; }
        if (cfg->invert_x) dx = -dx;
        if (cfg->invert_y) dy = -dy;

        if (dx || dy) {
            input_report_rel(d->dev, INPUT_REL_X, dx, false, K_NO_WAIT);
            input_report_rel(d->dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
        }
    }
    k_work_reschedule(&d->work, K_MSEC(cfg->poll_ms));
}

static int init(const struct device *dev) {
    struct data *d = dev->data;
    const struct cfg *cfg = dev->config;
    if (!device_is_ready(cfg->sensor)) return -ENODEV;
    d->dev = dev;
    k_work_init_delayable(&d->work, poll_work);
    k_work_reschedule(&d->work, K_MSEC(cfg->poll_ms));
    return 0;
}

#define INST(n)                                                            \
    static struct data data_##n;                                           \
    static const struct cfg cfg_##n = {                                    \
        .sensor = DEVICE_DT_GET(DT_INST_PHANDLE(n, sensor)),               \
        .poll_ms = DT_INST_PROP(n, poll_interval_ms),                      \
        .mult = DT_INST_PROP(n, scale_mult),                               \
        .div = DT_INST_PROP(n, scale_div),                                 \
        .swap_xy = DT_INST_PROP(n, swap_xy),                               \
        .invert_x = DT_INST_PROP(n, invert_x),                             \
        .invert_y = DT_INST_PROP(n, invert_y),                             \
    };                                                                     \
    DEVICE_DT_INST_DEFINE(n, init, NULL, &data_##n, &cfg_##n,              \
                          POST_KERNEL, CONFIG_ZMK_MPU6050_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INST)
