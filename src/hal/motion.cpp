#include "motion.h"

#include <SensorBMA423.hpp>
#include <Wire.h>

#include "config/pins.h"

namespace motion {

static SensorBMA423 s_bma;
static bool s_ok = false;

bool begin() {
    s_ok = s_bma.begin(Wire, BMA423_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!s_ok) {
        log_e("BMA423 nicht gefunden");
        return false;
    }
    // 100 Hz reichen für den Schrittzähler und sparen Strom gegenüber 200 Hz
    s_bma.configAccelerometer(SensorBMA423::RANGE_2G, SensorBMA423::ODR_100HZ);
    s_bma.enableAccelerometer();
    s_bma.enablePedometer();
    s_bma.enableFeature(SensorBMA423::FEATURE_STEP_CNTR, true);
    s_bma.resetPedometer();
    return true;
}

bool isOk() { return s_ok; }

uint32_t stepCount() { return s_ok ? s_bma.getPedometerCounter() : 0; }

void resetSteps() {
    if (s_ok) s_bma.resetPedometer();
}

}  // namespace motion
