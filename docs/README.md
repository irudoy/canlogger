# Документация

## Обзор проекта

- [REQUIREMENTS.md](REQUIREMENTS.md) — видение, роадмап, статус задач
- [ARCHITECTURE.md](ARCHITECTURE.md) — модули, задачи FreeRTOS, потоки данных, формат конфига, стратегия тестирования

## Пользовательские руководства

- [CONFIG_GUIDE.md](CONFIG_GUIDE.md) — как писать `config.ini`: поля, типы, scale/offset, LUT, plausibility-фильтр, пресеты, MLG-файлы, troubleshooting
- [DEBUG.md](DEBUG.md) — USB CDC CLI (`status`, `stream`, `config`, `ls`, `get`, `mark`, `settime`, `lastfault`), `usb_get.py`, загрузка конфигов через `cdc-put`

## Железо

- [HARDWARE.md](HARDWARE.md) — плата STM32_F4VE V2.0, пины, периферия, SWD, hat, E2E-стенд
- [HAT_PROTOTYPE.md](HAT_PROTOTYPE.md) — breadboard-прототип hat: DC-DC, supercap, VIN_SENSE делитель
- [PPK_MEASUREMENTS.md](PPK_MEASUREMENTS.md) — PPK2 измерения тока для расчёта supercap (graceful shutdown)
- [SCHEMATIC_CONVENTIONS.md](SCHEMATIC_CONVENTIONS.md) — правила оформления схем в KiCad

## Postmortem — завершённые исследования

- [postmortem/SD_ERRORS.md](postmortem/SD_ERRORS.md) — FR_DISK_ERR при длительной записи, root cause через `SD_status`/`validate()`, фикс 2026-04-10
- [postmortem/SD_WRITER_DECOUPLING.md](postmortem/SD_WRITER_DECOUPLING.md) — мотивация и план миграции на FreeRTOS (GC stalls до 710 мс блокировали main loop)
- [postmortem/STRESS_TEST_128U16_PLAN.md](postmortem/STRESS_TEST_128U16_PLAN.md) — финальный max-stress 128 U16 @ 1 kHz, 2ч 27м без ошибок
- [postmortem/CMD_RSP_TIMEOUT.md](postmortem/CMD_RSP_TIMEOUT.md) — природа CTIMEOUT при DMA-записи, CMD12/CMD13 при busy-карте

## Внешние референсы

- [reference/](reference/) — спецификация MLVLG (PDF), схема платы, референс rusEFI. См. [reference/README.md](reference/README.md)
