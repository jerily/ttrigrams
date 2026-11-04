execute_process(COMMAND "${NM}" -u "${MODULE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Cannot inspect module symbols: ${error}")
endif()
if(symbols MATCHES "(^|\n)[^\n]*[ \t]_?Tcl[A-Za-z_]")
    message(FATAL_ERROR "The shared module must call Tcl only through stubs: ${symbols}")
endif()
