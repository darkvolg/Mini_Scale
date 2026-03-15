#pragma once

// ================================================================
// Config.h — централизованная конфигурация проекта Mini_Scale
// ================================================================
// Все аппаратные пины, таймауты, пороги и настройки по умолчанию.
// Изменение любого параметра здесь применяется ко всем модулям.

// ===================== Debug =====================
// Сборка релиза: добавить в compiler flags -DMINI_SCALE_RELEASE
// В режиме release все DEBUG_* макросы разворачиваются в пустые выражения
// (zero-cost), Serial не инициализируется — экономим Flash и CPU.
#if !defined(MINI_SCALE_RELEASE)
  #define DEBUG_ENABLED
#endif

#ifdef DEBUG_ENABLED
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ===================== Pins =====================
// Wemos D1 Mini (ESP8266) пины
#define DOUT_PIN    D6   // HX711: пин данных (DOUT)
#define SCK_PIN     D5   // HX711: пин тактирования (SCK/PD_SCK)
#define BUTTON_PIN  D3   // Кнопка управления (активный LOW, INPUT_PULLUP)
#define BATTERY_PIN A0   // АЦП батареи (делитель напряжения: 4.2V → 3.2V)

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
#define DIM_FADE_STEP_MS      60    // интервал между шагами затухания (мс): 8×60=480мс
#define WAKE_FADE_STEPS       3     // количество шагов анимации пробуждения (быстрее)
#define WAKE_FADE_STEP_MS     40    // интервал между шагами пробуждения (мс): 3×40=120мс

// Уровни яркости для настройки пользователем
#define BRIGHTNESS_LOW   0x40  // низкая яркость
#define BRIGHTNESS_MED   0x8F  // средняя яркость
#define BRIGHTNESS_HIGH  0xCF  // высокая яркость (= NORMAL_BRIGHTNESS)

// ===================== Version =====================
#define FIRMWARE_VERSION          4    // текущая версия структуры EEPROM_Data
#define PREVIOUS_FIRMWARE_VERSION 3    // версия для миграции (v3 → v4)
#define FW_VERSION_STR            "v1.6.1"  // строка версии для отображения на заставке

// ===================== Defaults =====================
// Калибровочный коэффициент по умолчанию (единицы АЦП на килограмм).
// Примерное значение для типичного 5кг тензодатчика — требует калибровки под конкретный датчик.
#define DEFAULT_CALIBRATION 2280.0f

// ===================== Timers =====================
// Все значения в миллисекундах если не указано иное.

#define DEBOUNCE_MS             30        // антидребезг кнопки: ждём 30 мс и перепроверяем
#define LOOP_DELAY_MS           30        // задержка основного loop при активном измерении (~33 iter/s)
#define LOOP_DELAY_IDLE_MS      100       // задержка при idle/PowerSave (снижение потребления)
#define AUTO_OFF_MS             180000UL  // таймаут автовыключения по умолчанию (3 минуты)
#define AUTO_DIM_MS             60000UL   // таймаут автозатухания по умолчанию (1 минута)
#define AUTO_OFF_MSG_MS         7000      // время показа предупреждения перед автовыключением (7 сек)
#define CAL_ENTRY_WINDOW_MS     1000UL    // окно входа в калибровку после старта (1 сек)
#define CAL_LONG_PRESS_MS       800       // порог длинного нажатия в калибровке и меню (800 мс)
#define CAL_SAVED_MSG_MS        2000      // время показа "SAVED!" после сохранения (2 сек)
#define BUTTON_TARE_MS          10000UL   // удержание 10 сек → тарирование
#define BUTTON_UNDO_MS          15000UL   // удержание 15 сек → отмена тарирования
#define SUCCESS_MSG_MS          2000      // время показа сообщений "TARE OK!" и т.п. (2 сек)
#define HX711_INIT_DELAY_MS     500       // задержка после включения HX711 для стабилизации
#define HX711_TIMEOUT_MS        500       // таймаут ожидания готовности HX711 (мс)

// Параметры входа в меню настроек (двойное нажатие)
#define MENU_HOLD_MS            2000UL    // удержание 2 сек → показать "Press again"
#define MENU_CONFIRM_WINDOW_MS  3000UL    // окно ожидания второго нажатия (3 сек от отпускания)

// Таймауты бездействия в блокирующих режимах
#define SETTINGS_IDLE_TIMEOUT_MS  30000UL  // таймаут в меню настроек (30 сек)
#define CAL_IDLE_TIMEOUT_MS       60000UL  // таймаут в режиме калибровки (60 сек)

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
// LiPo max 4.2V → делитель → A0 max 3.2V (Wemos D1 Mini)
#define BAT_VOLTAGE_REF         3.2f    // максимальное напряжение на пине A0 (В)
#define BAT_DIVIDER_RATIO       0.762f  // коэффициент делителя = 3.2/4.2 ≈ 0.762
#define BAT_MIN_ADC_CONNECTED   50      // минимальный ADC при подключённой батарее (защита от отсутствия батареи)
#define BAT_LOW_PERCENT         10      // порог «низкий заряд»: иконка начинает мигать
#define BAT_CRITICAL_PERCENT    5       // порог «критический заряд»: начинаем выключение
#define BLINK_INTERVAL_MS       1500    // интервал мигания иконки при низком заряде (мс)
#define BAT_READ_INTERVAL_MS    5000UL  // интервал считывания АЦП батареи (каждые 5 сек)
#define BAT_GRACE_MS            10000UL // охранный период после старта: 10 сек без Battery_IsCritical()
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
// EMA-фильтр веса: filteredWeight = α*new + (1-α)*old
#define WEIGHT_EMA_ALPHA        0.3f    // α=0.3: 30% новое значение, 70% предыдущее
// Параметры авто-заморозки дисплея
#define WEIGHT_FREEZE_THRESHOLD   0.02f   // порог установки заморозки (20 г): меньше — замораживаем
#define WEIGHT_FREEZE_HYSTERESIS  0.01f   // гистерезис снятия заморозки: снимаем при >threshold+hysteresis (30 г)
#define HX711_ERROR_COUNT_MAX   3       // количество последовательных ошибок → переход в ERROR

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

// ===================== EEPROM =====================
// Wear-leveling: данные записываются round-robin по EEPROM_SLOTS слотам
#define EEPROM_SLOTS            4       // количество слотов wear-leveling
#define MAGIC_NUMBER            0x2A2B3CUL  // признак валидных данных в слоте
// Допустимый диапазон калибровочного коэффициента HX711
#define CAL_FACTOR_MIN          1.0f        // минимальный коэффициент (защита от деления на ноль)
#define CAL_FACTOR_MAX          10000.0f    // максимальный коэффициент (типичный диапазон 200..5000)
// Буфер стабильности: хранит последние STABILITY_WINDOW значений веса
#define STABILITY_WINDOW        8           // количество значений в буфере
#define STABILITY_THRESHOLD     0.03f       // max-min < 30 г → стабильный вес
#define SERIAL_BAUD             115200      // скорость UART для отладочного вывода
// Wear-leveling: минимальный интервал между записями — основная защита Flash
// Flash ESP8266 имеет ресурс ~100к циклов; 1 час × 4 слота × 100к = ~46 лет службы
#define EEPROM_MIN_INTERVAL_MS  3600000UL   // 1 час (мс)

// ===================== UI Defaults =====================
// Значения по умолчанию для настроек пользователя (индексы в таблицах)
#define DEFAULT_BRIGHTNESS_LEVEL  2    // HIGH (0=LOW, 1=MED, 2=HIGH)
#define DEFAULT_AUTO_OFF_MODE     1    // 3 минуты (0=1мин, 1=3мин, 2=5мин, 3=OFF)
#define DEFAULT_AUTO_DIM_MODE     1    // 60 секунд (0=30с, 1=60с, 2=120с)
#define DEFAULT_AUTO_ZERO_ON      1    // включён (0=выкл, 1=вкл)
#define DEFAULT_UNITS_MODE        0    // килограммы (0=кг, 1=г)
#define DEFAULT_TARA_LOCK_ON      0    // выключен (0=выкл, 1=вкл)

// Количество вариантов для таблиц SettingsMode (определяют размер массивов)
#define AUTO_OFF_VALUES_COUNT     4    // 1мин/3мин/5мин/OFF
#define AUTO_DIM_VALUES_COUNT     3    // 30с/60с/120с

// ===================== Smart Start =====================
// Минимальная разница веса для показа дельты при включении
#define SMART_START_MIN_DELTA     0.05f   // 50 г — меньше этого не показываем (шум/дрейф)
