#include "room_control.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_adc.h"
#include "stm32l4xx_hal_gpio.h"
#include "stm32l4xx_hal_rcc.h"
#include <string.h>
#include <stdio.h>

// Default password
static const char DEFAULT_PASSWORD[] = "1234";

// Temperature thresholds for automatic fan control
static const float TEMP_THRESHOLD_LOW = 25.0f;
static const float TEMP_THRESHOLD_MED = 28.0f;  
static const float TEMP_THRESHOLD_HIGH = 31.0f;

// Timeouts in milliseconds
static const uint32_t INPUT_TIMEOUT_MS = 10000;  // 10 seconds
static const uint32_t ACCESS_DENIED_TIMEOUT_MS = 3000;  // 3 seconds


//ADC configuration With PA0 as input
/**
* @brief ADC initialization function
* @retval None
*/  
ADC_HandleTypeDef hadc1;
void Room_ADC_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Configure PA0 as analog input (for ADC1 channel 1)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_1; // For PA0
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_6CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

// Private function prototypes
static void room_control_change_state(room_control_t *room, room_state_t new_state);
static void room_control_update_display(room_control_t *room);
static void room_control_update_door(room_control_t *room);
static void room_control_update_fan(room_control_t *room);
static fan_level_t room_control_calculate_fan_level(float temperature);
static void room_control_clear_input(room_control_t *room);

void room_control_init(room_control_t *room) {
    // Initialize room control structure
    room->current_state = ROOM_STATE_LOCKED;
    strcpy(room->password, DEFAULT_PASSWORD);
    room_control_clear_input(room);
    room->last_input_time = 0;
    room->state_enter_time = HAL_GetTick();
    
    // Initialize door control
    room->door_locked = true;
    
    // Initialize temperature and fan
    room->current_temperature = 22.0f;  // Default room temperature
    room->current_fan_level = FAN_LEVEL_OFF;
    room->manual_fan_override = false;
    
    // Display
    room->display_update_needed = true;
    
    // TODO: TAREA - Initialize hardware (door lock, fan PWM, etc.)
    // Ejemplo: HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_RESET);
}
/**
* @brief Change the current state of the room control system
 * @param room Pointer to the room control structure
 * @param new_state The new state to transition to
 */   
void room_control_update(room_control_t *room) {
    uint32_t current_time = HAL_GetTick();
    
    // State machine
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            // TODO: TAREA - Implementar lógica del estado LOCKED

            // - Mostrar mensaje "SISTEMA BLOQUEADO" en display
            ssd1306_Fill(Black);
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("SISTEMA", Font_7x10, White);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString("BLOQUEADO", Font_7x10, White);
            ssd1306_UpdateScreen(); // Update display
            // - Asegurar que la puerta esté cerrada
            room->door_locked = true;
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:
            // Check if input buffer is password length
            if (room->input_index >= 4) {
                // Compare input with stored password
                if (memcmp(room->input_buffer, room->password, 4) == 0) {
                    room_control_change_state(room, ROOM_STATE_UNLOCKED);
                } else {
                    room_control_change_state(room, ROOM_STATE_ACCESS_DENIED);
                }
            }
            //timeout handling
            if (current_time - room->last_input_time > INPUT_TIMEOUT_MS) {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        case ROOM_STATE_UNLOCKED:
            room->door_locked = false;  // Ensure door is unlocked
            // TODO: TAREA - Implementar lógica del estado UNLOCKED  
            // - Mostrar "ACCESO CONCEDIDO" y temperatura
            // - Mantener puerta abierta
            // - Permitir comandos de control manual
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            // TODO: TAREA - Implementar lógica de acceso denegado
            // - Mostrar "ACCESO DENEGADO" durante 3 segundos
            // - Enviar alerta a internet via ESP-01 (nuevo requerimiento)
            // - Volver automáticamente a LOCKED
            
            if (current_time - room->state_enter_time > ACCESS_DENIED_TIMEOUT_MS) {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        case ROOM_STATE_EMERGENCY:
            // TODO: TAREA - Implementar lógica de emergencia (opcional)
            break;
    }
    
    // Update subsystems
    room_control_update_door(room);
    room_control_update_fan(room);
    
    if (room->display_update_needed) {
        room_control_update_display(room);
        room->display_update_needed = false;
    }
}

/**
 * @brief Clear the input buffer and reset index
 * @param room Pointer to the room control structure
 */ 
void room_control_process_key(room_control_t *room, char key) {
    room->last_input_time = HAL_GetTick();
    
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            // Start password input
            room_control_clear_input(room);
            room->input_buffer[0] = key;
            room->input_index = 1;
            room_control_change_state(room, ROOM_STATE_INPUT_PASSWORD);
            
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:

            // TODO: TAREA - Implementar lógica de entrada de teclas
            // - Agregar tecla al buffer de entrada
            room->input_buffer[room->input_index] = key;  
            room->input_index++;
            // - Verificar si se completaron 4 dígitos
            if (room->input_index >= PASSWORD_LENGTH) {
               room_control_update(room);
            } 
            break;
                                        
        case ROOM_STATE_UNLOCKED:
            // TODO: TAREA - Manejar comandos en estado desbloqueado (opcional)
            // Ejemplo: tecla '*' para volver a bloquear
            if (key == '*') {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        default:
            break;
            
    
                                }
    
    room->display_update_needed = true;
}

void room_control_set_temperature(room_control_t *room, float temperature) {
    room->current_temperature = temperature;
    
    // Update fan level automatically if not in manual override
    if (!room->manual_fan_override) {
        fan_level_t new_level = room_control_calculate_fan_level(temperature);
        if (new_level != room->current_fan_level) {
            room->current_fan_level = new_level;
            room->display_update_needed = true;
        }
    }
}

void room_control_force_fan_level(room_control_t *room, fan_level_t level) {
    room->manual_fan_override = true;
    room->current_fan_level = level;
    room->display_update_needed = true;
}

void room_control_change_password(room_control_t *room, const char *new_password) {
    if (strlen(new_password) == PASSWORD_LENGTH) {
        strcpy(room->password, new_password);
    }
}

// Status getters
room_state_t room_control_get_state(room_control_t *room) {
    return room->current_state;
}

bool room_control_is_door_locked(room_control_t *room) {
    return room->door_locked;
}

fan_level_t room_control_get_fan_level(room_control_t *room) {
    return room->current_fan_level;
}
 
/**
    * @brief Get the current temperature from the ADC
    * @param room Pointer to the room control structure
    * @return Current temperature in Celsius
*/
float room_control_get_temperature(room_control_t *room) {
        // Read temperature from ADC
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        // Convert ADC value to temperature 
        room->current_temperature = (adc_value * 3.3f / 4095.0f) * 100.0f; // LM35 conversion
    return room->current_temperature;
}

// Private functions
static void room_control_change_state(room_control_t *room, room_state_t new_state) {
    room->current_state = new_state;
    room->state_enter_time = HAL_GetTick();
    room->display_update_needed = true;
    
    // State entry actions
    switch (new_state) {
        case ROOM_STATE_LOCKED:
            room->door_locked = true;
            room_control_clear_input(room);
            break;
            
        case ROOM_STATE_UNLOCKED:
            room->door_locked = false;
            room->manual_fan_override = false;  // Reset manual override
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            room_control_clear_input(room);
            break;
            
        default:
            break;
    }
}

/**
 * @brief Update the display based on the current state
 * @param room Pointer to the room control structure
 */

static void room_control_update_display(room_control_t *room) {
    char display_buffer[32];
    
    ssd1306_Fill(Black);
    
    // TODO: TAREA - Implementar actualización de pantalla según estado
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("SISTEMA", Font_7x10, White);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString("BLOQUEADO", Font_7x10, White);
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:
            ssd1306_Fill(Black);
            ssd1306_SetCursor(10, 10);
            //show received password on display
            ssd1306_WriteString("CLAVE:", Font_7x10, White);
            // Show asterisks for input
            for (uint8_t i = 0; i < room->input_index; i++) {
                ssd1306_WriteChar('*', Font_7x10, White);
            }
            break;
            
        case ROOM_STATE_UNLOCKED:
            // TODO: Mostrar estado del sistema (temperatura, ventilador)
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("ACCESO", Font_7x10, White);
            ssd1306_SetCursor(10, 20);
            ssd1306_WriteString("CONCEDIDO", Font_7x10, White);
            
            snprintf(display_buffer, sizeof(display_buffer), "Temp: %dC", (int)room->current_temperature);
            ssd1306_SetCursor(10, 30);
            ssd1306_WriteString(display_buffer, Font_7x10, White);
            
            snprintf(display_buffer, sizeof(display_buffer), "Fan: %d%%", room->current_fan_level);
            ssd1306_SetCursor(10, 40);
            ssd1306_WriteString(display_buffer, Font_7x10, White);
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("ACCESO", Font_7x10, White);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString("DENEGADO", Font_7x10, White);
            break;
            
        default:
            break;
    }
    
    ssd1306_UpdateScreen();
}

/**
 * @brief Update the door status based on the current state
 * @param room Pointer to the room control structure
 */

static void room_control_update_door(room_control_t *room) {
    // TODO: TAREA - Implementar control físico de la puerta
    // Ejemplo usando el pin DOOR_STATUS:
    if (room->door_locked) {
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_SET);
    }
}
/**
 * @brief Update the fan level based on the current temperature
 * @param room Pointer to the room control structure
 */
static void room_control_update_fan(room_control_t *room) {
    switch (room_control_calculate_fan_level(room_control_get_temperature(room))) {
    case FAN_LEVEL_OFF:
       room->current_fan_level = FAN_LEVEL_OFF;
        break;
    case FAN_LEVEL_LOW:
        room->current_fan_level = FAN_LEVEL_LOW;
        break;
    case FAN_LEVEL_MED:
        room->current_fan_level = FAN_LEVEL_MED;
        break;
    case FAN_LEVEL_HIGH:
        room->current_fan_level = FAN_LEVEL_HIGH;
        break;
    default:
        break;
    }
}

/**
 * @brief Calculate the fan level based on the current temperature
 * @param temperature Current temperature in Celsius
 * @return Fan level enumeration value
 */

static fan_level_t room_control_calculate_fan_level(float temperature) {
    // TODO: TAREA - Implementar lógica de niveles de ventilador
    if (temperature < TEMP_THRESHOLD_LOW) {
        return FAN_LEVEL_OFF;
    } else if (temperature < TEMP_THRESHOLD_MED) {
        return FAN_LEVEL_LOW;
    } else if (temperature < TEMP_THRESHOLD_HIGH) {
        return FAN_LEVEL_MED;
    } else {
        return FAN_LEVEL_HIGH;
    }
}

static void room_control_clear_input(room_control_t *room) {
    memset(room->input_buffer, 0, sizeof(room->input_buffer));
    room->input_index = 0;
}