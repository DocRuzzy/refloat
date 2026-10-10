// Copyright 2024 Syler Clayton
// Copyright 2025 Lukas Hrazky
//
// This file is part of the Refloat VESC package.
//
// Refloat VESC package is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// Refloat VESC package is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <http://www.gnu.org/licenses/>.

#include "bms.h"

#include <math.h>

void bms_init(BMS *bms) {
    bms->cell_lv = 0.0f;
    bms->cell_hv = 0.0f;
    bms->cell_lt = 0;
    bms->cell_ht = 0;
    bms->bms_ht = 0;
    bms->msg_age = 42.0f;
    bms->push_timer = 0;
    bms->ntc_temp = 0.0f;
    bms->ntc_timer = 0;
    bms->ntc_valid = false;
    bms->fault_mask = BMSF_NONE;
}

static inline void set_fault(uint32_t *fault_mask, BMSFaultCode fault_code) {
    *fault_mask |= 1u << (fault_code - 1);
}

static void check_cell_temps(uint32_t *fault_mask, const CfgBMS *cfg, float lt, float ht) {
    // Setting high temp threshold to 0 disables both high and low temp checking
    if (cfg->cell_ht_threshold > 0) {
        if (ht > cfg->cell_ht_threshold) {
            set_fault(fault_mask, BMSF_CELL_OVER_TEMP);
        }

        if (lt < cfg->cell_lt_threshold) {
            set_fault(fault_mask, BMSF_CELL_UNDER_TEMP);
        }
    }
}

void bms_update(BMS *bms, const CfgBMS *cfg, const Time *time) {
    if (!cfg->enabled) {
        bms->fault_mask = BMSF_NONE;
        return;
    }

    uint32_t fault_mask = BMSF_NONE;
    const float timeout = 5.0f;

    // Thermistor mode: the only data available is a single cell temperature,
    // voltage and balance checks are skipped. A missing or out-of-range
    // reading is reported as a connection fault, same as a lost CAN BMS.
    if (cfg->ntc.enabled) {
        if (!bms->ntc_valid || timer_older(time, bms->ntc_timer, timeout)) {
            if (time_elapsed(time, start, timeout)) {
                set_fault(&fault_mask, BMSF_CONNECTION);
            }
        } else {
            check_cell_temps(&fault_mask, cfg, bms->ntc_temp, bms->ntc_temp);
        }

        bms->fault_mask = fault_mask;
        return;
    }

    // Age the data by the time elapsed since the last push, so that it also
    // goes stale when pushes stop coming (e.g. the lisp loop stops running).
    float msg_age = bms->msg_age + timer_age(time, bms->push_timer);

    // Before the first BMS update occurs right after startup, msg_age has its
    // init value. Suppress all telemetry faults until the grace period expires.
    if (msg_age > timeout) {
        if (time_elapsed(time, start, timeout)) {
            set_fault(&fault_mask, BMSF_CONNECTION);
        }
        bms->fault_mask = fault_mask;
        return;
    }

    if (bms->cell_lv < cfg->cell_lv_threshold) {
        set_fault(&fault_mask, BMSF_CELL_UNDER_VOLTAGE);
    }

    if (bms->cell_hv > cfg->cell_hv_threshold) {
        set_fault(&fault_mask, BMSF_CELL_OVER_VOLTAGE);
    }

    check_cell_temps(&fault_mask, cfg, bms->cell_lt, bms->cell_ht);

    if (cfg->bms_ht_threshold > 0 && bms->bms_ht > cfg->bms_ht_threshold) {
        set_fault(&fault_mask, BMSF_OVER_TEMP);
    }

    if (fabsf(bms->cell_lv - bms->cell_hv) > cfg->cell_balance_threshold) {
        set_fault(&fault_mask, BMSF_CELL_BALANCE);
    }

    bms->fault_mask = fault_mask;
}

bool bms_ntc_update(BMS *bms, const CfgNTC *cfg, float voltage, const Time *time, float *temp) {
    // The pull-up goes to the same 3.3 V rail the ADC is referenced to, so the
    // reading is ratiometric.
    const float v_ref = 3.3f;
    // Readings this close to the rails mean an open (~3.3 V) or shorted (~0 V)
    // sensor. Also rejects the negative value returned for a missing channel.
    const float v_margin = 0.05f;
    // Exponential smoothing weight of a new sample, the loop runs at ~1 Hz.
    const float alpha = 0.3f;

    if (voltage < v_margin || voltage > v_ref - v_margin || cfg->r_nominal <= 0.0f ||
        cfg->r_pullup <= 0.0f || cfg->beta == 0) {
        return false;
    }

    // Voltage divider, NTC on the low side:
    //   V = Vref * R_ntc / (R_pullup + R_ntc)  =>  R_ntc = R_pullup * V / (Vref - V)
    const float r_ntc = cfg->r_pullup * voltage / (v_ref - voltage);

    // Beta form of the Steinhart-Hart equation (T in Kelvin, T0 = 25 °C):
    //   1 / T = 1 / T0 + ln(R_ntc / R_nominal) / Beta
    const float t_kelvin = 1.0f / (1.0f / 298.15f + logf(r_ntc / cfg->r_nominal) / cfg->beta);
    const float t = t_kelvin - 273.15f + cfg->temp_offset;

    // Restart the filter from the raw value if there was no valid reading
    // recently (first reading, or after a sensor fault).
    if (!bms->ntc_valid || timer_older(time, bms->ntc_timer, 5.0f)) {
        bms->ntc_temp = t;
    } else {
        bms->ntc_temp += alpha * (t - bms->ntc_temp);
    }
    timer_refresh(time, &bms->ntc_timer);
    bms->ntc_valid = true;

    *temp = bms->ntc_temp;
    return true;
}

bool bms_is_fault(const BMS *bms, BMSFaultCode fault_code) {
    return (bms->fault_mask & (1u << (fault_code - 1))) != 0;
}
