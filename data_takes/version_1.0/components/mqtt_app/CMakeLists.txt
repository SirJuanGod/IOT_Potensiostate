idf_component_register(SRCS "mqtt_app.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt
                    PRIV_REQUIRES config)
