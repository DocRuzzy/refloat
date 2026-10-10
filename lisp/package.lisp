(import "src/package_lib.bin" 'package-lib)
(load-native-lib package-lib)

(define fw-ver (sysinfo 'fw-ver))
(apply ext-set-fw-version fw-ver)

(define fw-bms-support (or (>= (first fw-ver) 7) (and (= (first fw-ver) 6) (>= (second fw-ver) 5))))

(if (ext-ntc)
    ; Start the battery thermistor loop if enabled, it replaces CAN BMS polling
    (if fw-bms-support
        (progn
            (import "ntc.lisp" 'ntc)
            (read-eval-program ntc)
            (spawn "Refloat NTC" 50 ntc-loop)
        )
        (print "[refloat] Battery Thermistor: Unsupported firmware version, 6.05+ required.")
    )
    ; Start the BMS polling loop in a thread if enabled
    (if (ext-bms)
        (if fw-bms-support
            (progn
                (import "bms.lisp" 'bms)
                (read-eval-program bms)
                (spawn "Refloat BMS" 50 bms-loop)
            )
            (print "[refloat] BMS Integration: Unsupported firmware version, 6.05+ required.")
        )
    )
)
