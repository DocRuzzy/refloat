(import "src/package_lib.bin" 'package-lib)
(load-native-lib package-lib)

(define fw-ver (sysinfo 'fw-ver))
(apply ext-set-fw-version fw-ver)

; Start the BMS polling loop in a thread if enabled
(if (ext-bms)
    (if (or (>= (first fw-ver) 7) (and (= (first fw-ver) 6) (>= (second fw-ver) 5)))
        (progn
            (import "bms.lisp" 'bms)
            (read-eval-program bms)
            (spawn "Refloat BMS" 50 bms-loop)
        )
        (print "[refloat] BMS Integration: Unsupported firmware version, 6.05+ required.")
    )
)

; Start the battery thermistor loop, it idles until the thermistor is enabled
(if (or (>= (first fw-ver) 7) (and (= (first fw-ver) 6) (>= (second fw-ver) 5)))
    (progn
        (import "ntc.lisp" 'ntc)
        (read-eval-program ntc)
        (spawn "Refloat NTC" 50 ntc-loop)
    )
)
