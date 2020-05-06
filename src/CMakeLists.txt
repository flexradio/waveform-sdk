set(WAVEFORM_SRCS
        utils.c
        )

set(WAVEFORM_HDRS
        utils.h
        )

add_library(waveform STATIC ${WAVEFORM_SRCS} ${WAVEFORM_HDRS})