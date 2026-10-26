#ifndef DISPLAY_H
#define DISPLAY_H

#pragma once

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Inicializa el display genérico.
 */
void display_init(void);

/**
 * @brief Muestra datos por pantalla.
 * 
 * @param line1 Linea 1 de los datos
 * @param line2 Linea 2 de los datos
 */
void display_show(const char *line1, const char *line2);

/**
 * @brief Desinicializa el display.
 */
void display_deinit(void);

/**
 * @brief Inicializa el display ST7789, LVGL y la interfaz gráfica de SquareLine.
 * @return esp_err_t ESP_OK si todo sale bien, o un código de error en caso contrario.
 */
esp_err_t display_gui_init(void);

/**
 * @brief Función para actualizar la temperatura y la humedad dentro del display.
 * 
 * @param temp Temperatura leída del sensor DHT11.
 * @param hum Humedad leída del sensor DHT11.
 * @param valid Booleano que indica si la lectura del sensor fue exitosa.
 */
void display_gui_update_data(uint8_t temp, uint8_t hum, bool valid);

#endif // DISPLAY_H