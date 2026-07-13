# ZMK Gyro Pointer (Air Mouse)

A [ZMK](https://zmk.dev) module that turns an MPU6050 gyroscope into a pointing device — tilt/rotate the keyboard to move the mouse cursor.

It ships as an **add-on shield** (`air_mouse`): it does not define a keyboard of its own, you list it *after* your existing keyboard shield and it layers the gyro pointer on top.

## Features

- MPU6050 gyro polled over I2C and reported as relative pointer motion through ZMK's pointing subsystem
- Works on any board that exposes `i2c0` — tested wiring notes for nice!nano and Seeed XIAO BLE below
- Sensitivity, poll rate, axis swap and axis inversion configurable from the devicetree
- Optional `zip_xy_transform` input processor hookup for runtime axis transforms

## Installation

Add this module to your `zmk-config`'s `config/west.yml` — add a new entry under `remotes` and one under `projects`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: EyalYe
      url-base: https://github.com/EyalYe
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-gyro-pointer                     # <-- add this project
      remote: EyalYe
      revision: main
  self:
    path: config
```

Then append `air_mouse` to the shield list of the build you want it on in `build.yaml`:

```yaml
include:
  - board: nice_nano//zmk
    shield: corne_left air_mouse
  - board: xiao_ble//zmk
    shield: my_board air_mouse
```

That's it — the driver enables itself automatically when the shield's devicetree nodes are present; no `.conf` changes are required.

### Local builds

If you build locally instead of with GitHub Actions, point ZMK at the module directly:

```sh
west build -b nice_nano//zmk -- \
  -DSHIELD="corne_left air_mouse" \
  -DZMK_EXTRA_MODULES=/path/to/zmk-gyro-pointer
```

## Wiring

Connect the MPU6050 to your controller's default I2C pins and power (VCC → 3.3 V, GND → GND):

| Board | SDA | SCL |
|---|---|---|
| nice!nano (pro-micro footprint) | D2 (P0.17) | D3 (P0.20) |
| Seeed XIAO BLE | D4 (P0.04) | D5 (P0.05) |

These pins come from each board's own devicetree (`i2c0` is preconfigured there), so no overlay changes are needed for either board.

### Custom pins / boards without i2c0 configured

If your board's devicetree does not configure `i2c0`, or you want different pins, uncomment the pinctrl block in
[`boards/shields/air_mouse/air_mouse.overlay`](boards/shields/air_mouse/air_mouse.overlay) and set your pins:

```dts
&i2c0 {
    status = "okay";
    compatible = "nordic,nrf-twi";
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";
    ...
};

&pinctrl {
    i2c0_default: i2c0_default {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, 17)>,   /* P0.17 -> your SDA pin */
                    <NRF_PSEL(TWIM_SCL, 0, 20)>;   /* P0.20 -> your SCL pin */
        };
    };

    i2c0_sleep: i2c0_sleep {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, 17)>,
                    <NRF_PSEL(TWIM_SCL, 0, 20)>;
            low-power-enable;
        };
    };
};
```

(The syntax above is for nRF52 boards; other SoCs use their own pinctrl bindings.)

## Usage

Once flashed, the cursor follows the gyro immediately: rotation around the Z axis (yaw) moves the cursor horizontally, rotation around the X axis (pitch) moves it vertically.

To tune it for your mounting orientation, override the `gyro_input` node from your keymap or a `<shield>.overlay` in your zmk-config:

```dts
&gyro_input {
    scale-div = <300>;     // higher = slower cursor
    layers = <1 2>;        // only move the cursor (and poll the gyro) on these layers
    invert-x;
    swap-xy;
};
```

Axis transforms can also be applied at the input-listener level with an input processor instead:

```dts
#include <dt-bindings/zmk/input_transform.h>

&gyro_listener {
    input-processors = <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>;
};
```

Mouse buttons are bound in your keymap as usual with [`&mkp`](https://zmk.dev/docs/keymaps/behaviors/mouse-button-press), e.g. `&mkp LCLK`.

## Configuration

Devicetree properties on the `zmk,mpu6050-input` node (`gyro_input`):

| Property | Description | Default |
|---|---|---|
| `sensor` | Phandle to the MPU6050 sensor node | (required) |
| `poll-interval-ms` | Gyro poll period in milliseconds | `10` |
| `scale-mult` | Sensitivity multiplier: counts per poll = rate (milli-rad/s) × mult ÷ div | `1` |
| `scale-div` | Sensitivity divisor (higher = slower) | `1` (shield sets `200`) |
| `layers` | Only poll the gyro while one of these layers is active; polling stops entirely on other layers (no motion, lower power). Omit to always poll | unset (shield sets `<1>`) |
| `swap-xy` | Swap the X and Y axes | off |
| `invert-x` | Invert horizontal movement | off |
| `invert-y` | Invert vertical movement | off |

Kconfig options:

| Option | Description | Default |
|---|---|---|
| `CONFIG_ZMK_MPU6050_INPUT` | Enables the driver | on when the DT node exists |
| `CONFIG_ZMK_MPU6050_INPUT_INIT_PRIORITY` | Driver init priority (must be later than `SENSOR_INIT_PRIORITY`) | `95` |
