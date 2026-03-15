# Аудит кода Mini Scale v1.6.1

Дата: 2026-03-04

Приоритеты: [КРИТИЧНО] — баг/потеря данных, [СРЕДНЕ] — надёжность/корректность, [НИЗКО] — улучшение/оптимизация

---

## ОШИБКИ И БАГИ

### 1. ~~[КРИТИЧНО] Auto-off обходит конечный автомат кнопки — прямой digitalRead~~  ✅ ИСПРАВЛЕНО
**Файл:** `Mini_Scale.ino` (блок auto-off)
**Было:** Блок отмены auto-off использовал прямой `digitalRead(BUTTON_PIN)` + `while(LOW)`, полностью обходя `ButtonControl`. После выхода состояние конечного автомата кнопки рассинхронизировано.
**Стало:** Используется `Button_Update()` — любое действие кнопки отменяет auto-off. Конечный автомат остаётся согласованным.
```
// FIX-1 в Mini_Scale.ino
ButtonAction offAction = Button_Update();
if (offAction != BTN_NONE) { ... }
```

---

### 2. ~~[КРИТИЧНО] Калибровка: `duration` вычисляется после второго `millis()`~~  ✅ ИСПРАВЛЕНО
**Файл:** `CalibrationMode.cpp`
**Было:** Два отдельных вызова `millis()` для `lastActionTime` и `duration`.
**Стало:** Один вызов `millis()` сохраняется в `releaseTime` и используется для обоих вычислений.
```
// FIX-2 в CalibrationMode.cpp
unsigned long releaseTime = millis();
lastActionTime = releaseTime;
unsigned long duration = releaseTime - pressTime;
```

---

### 3. ~~[СРЕДНЕ] `Memory_Save()` — last_weight без dirty-флага~~  ✅ ИСПРАВЛЕНО
**Файл:** `Mini_Scale.ino` (блок сохранения веса)
**Было:** `savedData.last_weight = current_weight` каждый loop без `Memory_MarkDirty()`.
**Стало:** Проверка `!=` + явный `Memory_MarkDirty()` при изменении.
```
// FIX-3 в Mini_Scale.ino
if (savedData.last_weight != current_weight) {
    savedData.last_weight = current_weight;
    Memory_MarkDirty();
}
```

---

### 4. ~~[СРЕДНЕ] `ApplySettings()` — порядок SetAutoZero/SetTaraLock хрупкий~~  ✅ ИСПРАВЛЕНО
**Файл:** `SettingsMode.cpp`
**Было:** Два вызова `Scale_SetAutoZero()` + `Scale_SetTaraLock()` — результат зависел от порядка строк.
**Стало:** Только один вызов `Scale_SetTaraLock()` — он единолично решает состояние `autoZeroEnabled` с учётом обоих флагов.
```
// FIX-4 в SettingsMode.cpp
// Убран Scale_SetAutoZero() — SetTaraLock сам читает savedData.auto_zero_on
Scale_SetTaraLock(savedData.tara_lock_on != 0);
```

---

### 5. ~~[СРЕДНЕ] `drawVoltage()` — потенциальное переполнение буфера~~  ✅ ИСПРАВЛЕНО
**Файл:** `DisplayControl.cpp`
**Было:** `dtostrf()` + ручной `'\0'` + `strcat("V")` — неочевидная логика обрезки.
**Стало:** `snprintf()` с ограничением по размеру буфера.
```
// FIX-5 в DisplayControl.cpp
snprintf(vBuf, sizeof(vBuf), "%.2fV", (double)voltage);
```

---

### 6. ~~[СРЕДНЕ] PowerSave — BTN_SHOW_HINT не сбрасывает таймер бездействия~~  ✅ ИСПРАВЛЕНО
**Файл:** `ScaleControl.cpp` (Scale_PowerSave)
**Было:** `BTN_SHOW_HINT` отбрасывался, `lastActivityTime` не обновлялось — dim/off мог сработать сразу после выхода из PowerSave.
**Стало:** Любое `a != BTN_NONE` обновляет `lastActivityTime`.
```
// FIX-6 в ScaleControl.cpp
if (a != BTN_NONE) {
    lastActivityTime = millis();
}
```

---

## ПОТЕНЦИАЛЬНЫЕ ПРОБЛЕМЫ

### 7. ~~[СРЕДНЕ] Переполнение millis() — несогласованные проверки~~  ✅ ИСПРАВЛЕНО
**Файлы:** `Mini_Scale.ino`, `DisplayControl.cpp`
**Было:** `>` в auto-off и Display_CheckDim вместо `>=`.
**Стало:** Везде `>=` — консистентно с `CoreLogic::TimeoutElapsed`.
```
// FIX-7 в Mini_Scale.ino и DisplayControl.cpp
millis() - lastActivityTime >= activeAutoOffMs   // было >
now - lastActivity >= autoDimMs                   // было >
```

---

### 8. ~~[СРЕДНЕ] EEPROM_Data — padding и выравнивание структуры~~  ✅ ИСПРАВЛЕНО
**Файлы:** `MemoryControl.h`, `MemoryControl.cpp`
**Было:** Структуры без `packed` — компилятор мог вставить padding, ломая CRC при смене тулчейна.
**Стало:** `__attribute__((packed))` на всех 3 структурах: `EEPROM_Data`, `EEPROM_Data_V2`, `EEPROM_Data_V3`.
```
// FIX-8
struct __attribute__((packed)) EEPROM_Data { ... };
struct __attribute__((packed)) EEPROM_Data_V2 { ... };
struct __attribute__((packed)) EEPROM_Data_V3 { ... };
```
**⚠️ ВНИМАНИЕ:** Если у существующих устройств EEPROM был записан без packed, layout может отличаться. На ESP8266/Xtensa GCC layout скорее всего идентичен (4-byte alignment для long/float совпадает), но рекомендуется проверить `sizeof()` до и после на целевой платформе. Если отличается — нужна миграция.

---

### 9. ~~[СРЕДНЕ] UiText.h — static constexpr дублирует указатели в каждом TU~~  ✅ ИСПРАВЛЕНО
**Файлы:** `UiText.h`, новый `UiText.cpp`
**Было:** `static constexpr const char*` — каждый .cpp получал свою копию указателей (~60-80 байт RAM).
**Стало:** `extern const char* const` в `.h` + единственные определения в `UiText.cpp`.
```
// FIX-9: UiText.h
extern const char* const kLowBattery;
// FIX-9: UiText.cpp (новый файл)
const char* const kLowBattery = "LOW BATTERY!";
```

---

### 10. ~~[НИЗКО] session_delta не обновляется в основном цикле~~  ✅ ИСПРАВЛЕНО
**Файлы:** `ScaleControl.cpp`
**Было:** `session_delta` вычислялась один раз в `Scale_Init()` и обнулялась при `Scale_Tare()`. Дрейф и auto-zero делали её неактуальной.
**Стало:** Добавлен `initialSessionWeight`, `session_delta` пересчитывается на каждом `Scale_Update()`. Корректно обновляется при Tare/Undo.
```
// FIX-10 в ScaleControl.cpp
static float initialSessionWeight = 0.0f;
// В Scale_Init():   initialSessionWeight = startup_weight;
// В Scale_Update():  session_delta = current_weight - initialSessionWeight;
// В Scale_Tare():    initialSessionWeight = 0.0f;
// В Scale_UndoTare(): initialSessionWeight = savedData.last_weight;
```

---

## РЕЗЮМЕ

| Приоритет | Кол-во | Исправлено |
|-----------|--------|------------|
| КРИТИЧНО  | 2      | ✅ 2/2     |
| СРЕДНЕ    | 6      | ✅ 6/6     |
| НИЗКО     | 2      | ✅ 2/2     |
| **Итого** | **10** | **✅ 10/10** |

### Изменённые файлы:
- `Mini_Scale.ino` — FIX-1, FIX-3, FIX-7
- `CalibrationMode.cpp` — FIX-2
- `SettingsMode.cpp` — FIX-4
- `DisplayControl.cpp` — FIX-5, FIX-7
- `ScaleControl.cpp` — FIX-6, FIX-10
- `MemoryControl.h` — FIX-8
- `MemoryControl.cpp` — FIX-8
- `UiText.h` — FIX-9 (переписан)
- `UiText.cpp` — FIX-9 (новый файл)

---

## ОСТАВШИЕСЯ ПУНКТЫ ИЗ ОРИГИНАЛЬНОГО АУДИТА (11-20)

Эти пункты не были в запросе на исправление, но задокументированы для дальнейшей работы:

### 11. [НИЗКО] Scale_Update — восстановление из ERROR неполное
Использовать `resetBuffers()` вместо ручного частичного сброса.

### 12. [НИЗКО] CalibrationMode — cal_factor может уйти в минус на 1 кадр
Делать clamp сразу после изменения, до `scale.set_scale()`.

### 13. [НИЗКО] Smart Start: `delay(3000)` без wdtFeed в setup()
Добавить `ESP.wdtFeed()` перед `delay(3000)`.

### 14. [НИЗКО] `delay(1500)` для splash без wdtFeed
Добавить `ESP.wdtFeed()` перед длинными delay в setup.

### 15. [НИЗКО] Нет превью для Auto-Zero и Tara Lock в SettingsMode
Опционально.

### 16. [НИЗКО] Battery EMA инициализируется одним сэмплом ADC
Усреднить 4-8 чтений.

### 17. [НИЗКО] BAT_DIVIDER_RATIO = 1.0 — проверить по аппаратной схеме
Зависит от резисторов делителя.

### 18. [НИЗКО] millis() вызывается многократно без кэширования
Кэшировать `now` в начале loop().

### 19. [НИЗКО] Глобальные static для transient message в .ino
Опционально вынести в модуль.

### 20. [НИЗКО] EEPROM_SIZE_COMPUTED + 16 байт не обоснованы
Убрать или задокументировать.


21. Web OTA (AP + веб-страница) — самый дружественный
Как работает:

Пользователь активирует OTA-режим (например, удержание кнопки при включении)
Весы поднимают свою WiFi-точку (AP mode): MiniScale-Update
Пользователь подключается к ней с телефона/ноутбука
Открывает 192.168.4.1 в браузере
Выбирает .bin файл, жмёт "Upload"
Готово
Что нужно новичку:

Скачать .bin файл (ты выкладываешь на GitHub Releases)
Подключиться к WiFi-точке весов
Открыть страницу в браузере и загрузить файл
