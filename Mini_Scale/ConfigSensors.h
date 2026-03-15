#pragma once

// ================================================================
// ConfigSensors.h — параметры датчиков, дисплея, батареи, EEPROM
// ================================================================

// ===================== Display =====================
// OLED дисплей SSD1306, 128x64 пикселей, I2C интерфейс
#define SCREEN_WIDTH    128   // ширина дисплея в пикселях
#define SCREEN_HEIGHT   64    // высота дисплея в пикселях
#define OLED_I2C_ADDR   0x3C  // стандартный I2C адрес SSD1306 (альтернатива 0x3D)
#define OLED_RESET_PIN  (-1)  // reset пин не используется (подключён к RST ESP)

// Размеры иконки батареи в пикселях
#define BAT_ICON_W   24   // ширина корпуса иконки батареи
#define BAT_ICON_H   10   // высота корпуса иконки батареи

// Яркость дисплея: значения регистра SSD1306 CONTRAST (0x00..0xFF)
#define DIM_BRIGHTNESS        0x00  // затемнённая яркость (почти выключен)
#define NORMAL_BRIGHTNESS     0xCF  // яркость по умолчанию (соответствует HIGH)
#define DIM_FADE_STEPS        8     // количество шагов анимации затухания
#define DIM_FADE_STEP_MS      60    // интервал между шагами затухания (мс): 8x60=480мс
#define WAKE_FADE_STEPS       3     // количество шагов анимации пробуждения (быстрее)
#define WAKE_FADE_STEP_MS     40    // интервал между шагами пробуждения (мс): 3x40=120мс

// Уровни яркости для настройки пользователем
#define BRIGHTNESS_LOW   0x40  // низкая яркость
#define BRIGHTNESS_MED   0x8F  // средняя яркость
#define BRIGHTNESS_HIGH  0xCF  // высокая яркость (= NORMAL_BRIGHTNESS)

// ===================== HX711 =====================
// Количество усреднений АЦП: больше = точнее, но медленнее
#define HX711_SAMPLES_STARTUP   10  // при старте — больше для лучшей точности
#define HX711_SAMPLES_READ      3   // в loop() — компромисс скорость/точность
#define HX711_SAMPLES_TARE      10  // при тарировании — больше для точного нуля
#define HX711_SAMPLES_UNDO      5   // при отмене тарирования
#define HX711_SAMPLES_CAL       3   // в режиме калибровки

// ===================== Battery =====================
// Параметры EMA-фильтра АЦП батареи: smoothed = EMA_OLD*old + EMA_NEW*new
#define BAT_EMA_OLD             0.7f   // вес предыдущего значения (70% инерция)
#define BAT_EMA_NEW             0.3f   // вес нового значения (30% чувствительность)
#define BAT_ADC_MAX             1023.0f  // максимальное значение АЦП ESP8266 (10 бит)
// Параметры делителя напряжения (внешний резистор):
// LiPo max 4.2V -> делитель -> A0 max 3.2V (Wemos D1 Mini)
#define BAT_VOLTAGE_REF         3.2f    // максимальное напряжение на пине A0 (В)
#define BAT_DIVIDER_RATIO       0.762f  // коэффициент делителя = 3.2/4.2 ~ 0.762
#define BAT_MIN_ADC_CONNECTED   50      // минимальный ADC при подключённой батарее (защита от отсутствия батареи)
#define BAT_LOW_PERCENT         10      // порог «низкий заряд»: иконка начинает мигать
#define BAT_CRITICAL_PERCENT    5       // порог «критический заряд»: начинаем выключение
#define BAT_PROFILE_LIPO        1       // 1 = кусочно-линейная кривая LiPo, 0 = линейная
#define BAT_LINEAR_EMPTY_V      3.20f   // напряжение «пустая» для линейного профиля (В)
#define BAT_LINEAR_FULL_V       4.20f   // напряжение «полная» для линейного профиля (В)

// ===================== Weight =====================
// Флаг ошибки: значение < WEIGHT_ERROR_THRESHOLD означает ошибку измерения
#define WEIGHT_ERROR_FLAG       (-99.9f)  // сигнальное значение ошибки HX711
#define WEIGHT_ERROR_THRESHOLD  (-99.0f)  // всё что меньше этого порога = ошибка
// Пороги детектирования изменений
#define WEIGHT_CHANGE_THRESHOLD 0.05f   // минимальное изменение веса для обновления (50 г)
#define WEIGHT_SANE_MAX         500.0f  // максимальный «разумный» вес для проверки sanity (кг)
// EMA-фильтр веса: filteredWeight = a*new + (1-a)*old
#define WEIGHT_EMA_ALPHA        0.3f    // a=0.3: 30% новое значение, 70% предыдущее
// Параметры авто-заморозки дисплея
#define WEIGHT_FREEZE_THRESHOLD   0.02f   // порог установки заморозки (20 г): меньше — замораживаем
#define WEIGHT_FREEZE_HYSTERESIS  0.01f   // гистерезис снятия заморозки: снимаем при >threshold+hysteresis (30 г)
#define HX711_ERROR_COUNT_MAX   3       // количество последовательных ошибок -> переход в ERROR

// Медианный фильтр: буфер из 3 значений для удаления одиночных выбросов
#define MEDIAN_WINDOW           3

// Auto-zero tracking: параметры алгоритма постепенной коррекции нуля
#define AUTOZERO_THRESHOLD      0.05f   // порог активации: |вес| < 50 г
#define AUTOZERO_STEP           1       // шаг коррекции offset (1 единица АЦП HX711)
#define AUTOZERO_INTERVAL_MS    3000UL  // минимальный интервал между коррекциями (3 сек)
#define AUTOZERO_MIN_STABLE_CYCLES 5    // минимум стабильных циклов перед первой коррекцией

// Перегрузка и тренд
#define WEIGHT_OVERLOAD_KG      5.0f    // максимальный вес до перегрузки (кг)
#define TREND_THRESHOLD         0.03f   // минимальное изменение для детектирования тренда (30 г/итерацию)

// Калибровочный коэффициент по умолчанию (единицы АЦП на килограмм).
// Примерное значение для типичного 5кг тензодатчика — требует калибровки под конкретный датчик.
#define DEFAULT_CALIBRATION 2280.0f
// Допустимый диапазон калибровочного коэффициента HX711
#define CAL_FACTOR_MIN          1.0f        // минимальный коэффициент (защита от деления на ноль)
#define CAL_FACTOR_MAX          10000.0f    // максимальный коэффициент (типичный диапазон 200..5000)

// ===================== EEPROM =====================
// Wear-leveling: данные записываются round-robin по EEPROM_SLOTS слотам
#define EEPROM_SLOTS            4       // количество слотов wear-leveling
#define MAGIC_NUMBER            0x2A2B3CUL  // признак валидных данных в слоте
// Буфер стабильности: хранит последние STABILITY_WINDOW значений веса
#define STABILITY_WINDOW        8           // количество значений в буфере
#define STABILITY_THRESHOLD     0.03f       // max-min < 30 г -> стабильный вес
// Wear-leveling: минимальный интервал между записями — основная защита Flash
// Flash ESP8266 имеет ресурс ~100к циклов; 1 час x 4 слота x 100к = ~46 лет службы
#define EEPROM_MIN_INTERVAL_MS  3600000UL   // 1 час (мс)
