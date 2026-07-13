#define DT_DRV_COMPAT zmk_mpu6050_input

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mpu6050_input, CONFIG_ZMK_LOG_LEVEL);

struct cfg {
    const struct device *sensor;
    uint16_t poll_ms;
    int32_t mult, div;
    bool swap_xy, invert_x, invert_y;
    const int32_t *layers;
    size_t layers_len;
};

struct data {
    const struct device *dev;
    struct k_work_delayable work;
    bool polling;
    uint32_t polls;
};

static void poll_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct data *d = CONTAINER_OF(dwork, struct data, work);
    const struct cfg *cfg = d->dev->config;
    struct sensor_value g[3];

    if (!d->polling) return;

    int rc = sensor_sample_fetch(cfg->sensor);
    if (rc == 0) rc = sensor_channel_get(cfg->sensor, SENSOR_CHAN_GYRO_XYZ, g);

    if (rc == 0) {
        /* yaw (gz) -> X, pitch (gx or gy) -> Y; tune to mounting.
           Rates are milli-rad/s; counts per poll = milli * mult / div */
        int32_t mz = (int32_t)sensor_value_to_milli(&g[2]);
        int32_t mx = (int32_t)sensor_value_to_milli(&g[0]);
        int32_t dx = mz * cfg->mult / cfg->div;
        int32_t dy = mx * cfg->mult / cfg->div;

        /* pmw3610-style transforms */
        if (cfg->swap_xy) { int32_t t = dx; dx = dy; dy = t; }
        if (cfg->invert_x) dx = -dx;
        if (cfg->invert_y) dy = -dy;

        /* one debug line per second at the default 10ms poll */
        if (d->polls++ % 100 == 0) {
            LOG_DBG("gyro mrad/s x=%d z=%d -> dx=%d dy=%d", mx, mz, dx, dy);
        }

        if (dx || dy) {
            input_report_rel(d->dev, INPUT_REL_X, dx, false, K_NO_WAIT);
            input_report_rel(d->dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
        }
    } else if (d->polls++ % 100 == 0) {
        LOG_WRN("gyro read failed (%d)", rc);
    }
    k_work_reschedule(&d->work, K_MSEC(cfg->poll_ms));
}

/* no layers property -> always poll; otherwise poll while any listed layer is active */
static bool should_poll(const struct cfg *cfg) {
    if (cfg->layers_len == 0) return true;
    for (size_t i = 0; i < cfg->layers_len; i++) {
        if (zmk_keymap_layer_active(cfg->layers[i])) return true;
    }
    return false;
}

static void update_polling(const struct device *dev) {
    struct data *d = dev->data;
    bool on = should_poll(dev->config);

    if (on == d->polling) return;
    d->polling = on;
    LOG_INF("gyro polling %s", on ? "started" : "stopped");
    if (on) {
        k_work_reschedule(&d->work, K_NO_WAIT);
    } else {
        k_work_cancel_delayable(&d->work);
    }
}

static int layer_state_listener(const zmk_event_t *eh) {
#define UPDATE_INST(n) update_polling(DEVICE_DT_INST_GET(n));
    DT_INST_FOREACH_STATUS_OKAY(UPDATE_INST)
#undef UPDATE_INST
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(mpu6050_input, layer_state_listener);
ZMK_SUBSCRIPTION(mpu6050_input, zmk_layer_state_changed);

static int init(const struct device *dev) {
    struct data *d = dev->data;
    const struct cfg *cfg = dev->config;
    if (!device_is_ready(cfg->sensor)) {
        LOG_ERR("MPU6050 sensor not ready (check wiring/I2C address)");
        return -ENODEV;
    }
    d->dev = dev;
    k_work_init_delayable(&d->work, poll_work);
    update_polling(dev);
    LOG_INF("mpu6050 input up: poll %ums, scale %d/%d, %u gated layers, polling=%d",
            cfg->poll_ms, cfg->mult, cfg->div, (unsigned)cfg->layers_len, d->polling);
    return 0;
}

#define INST(n)                                                            \
    static struct data data_##n;                                           \
    static const int32_t layers_##n[] = DT_INST_PROP_OR(n, layers, {});    \
    static const struct cfg cfg_##n = {                                    \
        .sensor = DEVICE_DT_GET(DT_INST_PHANDLE(n, sensor)),               \
        .poll_ms = DT_INST_PROP(n, poll_interval_ms),                      \
        .mult = DT_INST_PROP(n, scale_mult),                               \
        .div = DT_INST_PROP(n, scale_div),                                 \
        .swap_xy = DT_INST_PROP(n, swap_xy),                               \
        .invert_x = DT_INST_PROP(n, invert_x),                             \
        .invert_y = DT_INST_PROP(n, invert_y),                             \
        .layers = layers_##n,                                              \
        .layers_len = ARRAY_SIZE(layers_##n),                              \
    };                                                                     \
    DEVICE_DT_INST_DEFINE(n, init, NULL, &data_##n, &cfg_##n,              \
                          POST_KERNEL, CONFIG_ZMK_MPU6050_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INST)
