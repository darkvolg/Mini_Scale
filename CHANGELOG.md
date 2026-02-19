# Mini_Scale — Аннотация проделанной работы

## Что это
Компактные электронные весы на **Wemos D1 Mini (ESP8266)** с OLED SSD1306, датчиком веса HX711, одной кнопкой и LiPo батареей.

## История коммитов

| # | Коммит | Описание |
|---|--------|----------|
| 1 | `cb8926e` | **Initial commit** — базовый скетч, всё в одном файле |
| 2 | `bb61c06` | Добавлен README с описанием проекта, пинами, инструкцией |
| 3 | `3d837ce` | Создан Bug Report HTML с 6 найденными багами |
| 4 | `57ccf74` | **Исправлены 6 багов**: auto-off на основе активности, `unsigned long` для `millis()`, debounce, сравнение float, формула батареи |
| 5 | `893f556` | Удалён Bug_Report.html (баги исправлены) |
| 6 | `382c57a` | Удалена неиспользуемая директория config |
| 7 | `f99f3dc` | **Исправлены 11 багов**: формула батареи, auto-off, debounce калибровки, UndoTare дельта, шум HX711 |
| 8 | `bedcb50` | **Фикс**: валидация EEPROM, debounce, `math.h`, UX калибровки |
| 9 | `4893483` | **10 улучшений**: стабилизация, splash-экран, низкий заряд, auto-dim, watchdog, HX711 timeout, EEPROM throttle, разделение .h/.cpp, неблокирующая кнопка, константы |
| 10 | `24e1a06` | **Фикс**: `getTextBounds` для центрирования splash, периодическое сохранение EEPROM |

## Архитектура (после рефакторинга)

Проект разбит на 7 модулей (.h + .cpp):

| Файл | Назначение |
|------|-----------|
| `Config.h` | Все пины и константы в одном месте |
| `Mini_Scale.ino` | Главный loop: вес → батарея → кнопка → дисплей → auto-dim/off |
| `ScaleControl` | HX711: чтение, тарирование, undo tare, стабилизация (ring buffer) |
| `DisplayControl` | OLED: main screen, splash с progress bar, dim/wake, сообщения |
| `ButtonControl` | State machine (IDLE→PRESSED→HOLDING→RELEASED), неблокирующая |
| `CalibrationMode` | Режим калибровки: 5 подменю (+10, -10, +1, -1, SAVE) |
| `MemoryControl` | EEPROM с magic number, валидацией, throttling (раз в 5 мин) |

## Пины

| Пин | Подключение |
|-----|-------------|
| D6 | HX711 DOUT |
| D5 | HX711 SCK |
| D1 | OLED SCL (I2C) |
| D2 | OLED SDA (I2C) |
| D3 | Кнопка (INPUT_PULLUP) |
| A0 | Батарея (через делитель, Vref=3.2V) |

## Ключевые константы

- **Auto-off**: 180 сек (3 мин) неактивности → deep sleep
- **Auto-dim**: 30 сек неактивности → приглушение дисплея
- **Кнопка**: удержание 5с = Tare, 10с = Undo Tare
- **Калибровка**: вход — зажать кнопку при загрузке (окно 1 сек), длинное нажатие >0.8с = смена режима
- **Стабилизация**: ring buffer 8 замеров, порог 0.03 кг
- **Батарея**: EMA сглаживание (0.9/0.1), low=10%, critical=5% → shutdown
- **EEPROM**: throttle записи — не чаще раз в 5 мин (кроме force save при tare/calibration)
- **Default calibration**: 2280.0

## Все реализованные функции

1. **Измерение веса** — HX711, 3 сэмпла на чтение, отображение в кг с 2 знаками
2. **Дельта сессии** — разница текущего веса и последнего сохранённого
3. **Индикатор стабильности** — `=` (стабильно) / `~` (колеблется)
4. **Tare** — обнуление, backup offset для undo
5. **Undo Tare** — восстановление предыдущего offset и last_weight
6. **Калибровка** — 5 режимов: +10, -10, +1, -1, SAVE & EXIT
7. **Splash screen** — "Mini Scale" с progress bar при загрузке
8. **Auto-dim** — приглушение экрана через 30 сек
9. **Auto power off** — deep sleep через 3 мин
10. **Мониторинг батареи** — piecewise linear LiPo%, EMA сглаживание
11. **Low battery blink** — мигание строки батареи при <10%
12. **Critical shutdown** — deep sleep при <5% (с grace period 10 циклов)
13. **EEPROM persistence** — magic number, валидация NaN/Inf, throttled save
14. **Watchdog** — `ESP.wdtFeed()` в loop и калибровке
15. **HX711 timeout** — `wait_ready_timeout(500ms)` вместо бесконечного ожидания
16. **Неблокирующая кнопка** — state machine вместо blocking `while`

## Библиотеки (Arduino)

- `HX711` (bogde)
- `Adafruit SSD1306`
- `Adafruit GFX`
- `Wire` (встроенная)
- `EEPROM` (встроенная ESP8266)
