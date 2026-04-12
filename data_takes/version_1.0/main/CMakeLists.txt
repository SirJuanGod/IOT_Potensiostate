idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES potenciostato_core wifi_manager mqtt_app config)
