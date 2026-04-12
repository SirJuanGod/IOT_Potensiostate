idf_component_register(SRCS "potenciostato_core.c"
                    INCLUDE_DIRS "include"
                    REQUIRES i2c_driver spi_driver mux_driver mqtt_app
                    PRIV_REQUIRES config)
