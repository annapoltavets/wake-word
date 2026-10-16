set(EI_SDK_FOLDER ${CMAKE_CURRENT_LIST_DIR}/edge-impulse-sdk)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # оптимізації ESP-NN / ESP-DSP від Espressif
    add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1)
    add_definitions(-DEIDSP_USE_ESP_DSP=1)
    if(${IDF_TARGET} STREQUAL "esp32s3")
        add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN_S3=1)
    endif()
endif()

include(${EI_SDK_FOLDER}/cmake/utils.cmake)

RECURSIVE_FIND_FILE_EXCLUDE_DIR(SOURCE_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cpp")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(MODEL_FILES "${CMAKE_CURRENT_LIST_DIR}/tflite-model" "CMSIS" "*.cpp")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(CC_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cc")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(C_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.c")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_NN "${EI_SDK_FOLDER}/porting/espressif/ESP-NN" "CMSIS" "*.s")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_DSP "${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft" "CMSIS" "*.s")

list(APPEND SOURCE_FILES ${C_FILES} ${CC_FILES} ${MODEL_FILES} ${S_FILES_ESP_NN} ${S_FILES_ESP_DSP})

idf_component_register(SRCS "ei_wrapper.cpp" ${SOURCE_FILES}
                       REQUIRES esp_timer driver
                       INCLUDE_DIRS "."
                                    "model-parameters"
                                    "tflite-model"
                                    "edge-impulse-sdk"
                                    "edge-impulse-sdk/porting/espressif/esp-dsp/modules/fft/include"
                                    "edge-impulse-sdk/porting/espressif/esp-dsp/modules/common/include")

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    -Wno-unused-variable -Wno-maybe-uninitialized -Wno-error=format= -Wno-unused-but-set-parameter
    -Wno-error=nonnull-compare -Wno-error=stringop-truncation -Wno-error=uninitialized
    -Wno-stringop-truncation -Wno-deprecated-declarations -fno-if-conversion)
