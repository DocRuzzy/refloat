; Battery thermistor loop. Reads the NTC divider on ADC3, lets the package
; convert it (and feed the BMS alerts) via ext-ntc, and publishes the
; temperature into the firmware BMS data so telemetry apps can show it.
;
; Intended for packs without a CAN BMS: this writes the same BMS values a CAN
; BMS would, so the two shouldn't be used together.

(defun ntc-publish (temp) {
    ; A single sensor is min = max = avg, fill all cell temp slots so apps
    ; aggregating over them don't pick up phantom 0 °C readings.
    (set-bms-val 'bms-temp-adc-num 3)
    (looprange i 0 3 (set-bms-val 'bms-temps-adc i temp))
    (set-bms-val 'bms-temp-cell-max temp)
})

(defun ntc-loop () {
    (var err-print-time nil)
    (var was-enabled nil)
    (loopwhile t {
        (var res (trap {
            ; Polled every loop so enabling / disabling doesn't need a restart.
            ; While disabled, leave the BMS data alone (a CAN BMS may own it).
            (var enabled (ext-ntc))
            (if enabled {
                ; get-adc channel 2 is ADC3
                (var temp (ext-ntc (get-adc 2)))
                (if temp
                    (ntc-publish temp)
                    ; Open or shorted sensor: report no temperatures rather
                    ; than leaving a stale value in place.
                    (set-bms-val 'bms-temp-adc-num 0)
                )
            } {
                ; Just disabled: clear what we published
                (if was-enabled (set-bms-val 'bms-temp-adc-num 0))
            })
            (setq was-enabled enabled)
        }))

        (if (and (eq (first res) 'exit-error)
                 (or (not err-print-time) (>= (secs-since err-print-time) 5))) {
            (setq err-print-time (systime))
            (print (str-merge "[refloat] Thermistor loop error: " (to-str (second res))))
        })

        (sleep 1.0)
    })
})
