# MINOSPRIME (pinos-mrime) v0.2 — full C++ implementation

Не задаётся вопросов по умолчанию (R1): любой параметр юнита либо задан явно,
либо юнит помечен красным и схема валидатором помечена как дефектная.

## Состав

- `src/mp/json.*`      собственный JSON reader/writer (без внешних зависимостей)
- `src/mp/model.*`     документ: нейроны (1.1–1.28), связи (2.1–2.11), зоны, кластеры (3.1–3.4),
                       корректное удаление юнитов (R4), счётчики ID (1.1)
- `src/mp/serialize.cpp` Save/Load ВСЕХ полей (R5: refrMax, disabled, счётчики ID)
- `src/mp/undo.cpp`    единая история мыши И кода, глубина 1024 (7.3), история сборок кода (4.6)
- `src/mp/dsl.cpp`     OpenSCAD-модель DSL (4.1–4.6), сборка как операция НАД документом (R2),
                       ошибки с номером строки, экспорт кода из схемы (4.4)
- `src/mp/engine.cpp`  такт симуляции: LIF/адаптивный LIF, рефракт, шум, след,
                       пластичность (жёсткая/Хебб/reward-gated/анти-Хебб), распад,
                       модуляторы (дофамин/наказание/новизна) с индивидуальными окном/радиусом/ставкой,
                       гомеостат (1.26–1.28)
- `src/mp/validator.cpp` красные/жёлтые флаги (8.1–8.2)
- `src/mp/stands.cpp`  стенды: запись/воспроизведение, хеш схемы, регресс-прогон (9.1–9.4)
- `src/mp/api.cpp`     HTTP API (loopback): get_neuron, set_sensor(s), get_schema, stream (10.1–10.5)
- `src/main_headless.cpp` headless CLI: validate / serve (API) / run-stands
- `tests/`             юнит-тесты K1–K12 (headless-часть) и правил R1–R5

Графический редактор (SDL2/OpenGL, раздел 5 директив) — отдельная цель следующей итерации,
в ядро v0.2 он не входит: всё ядро headless и покрыто тестами.

## Сборка и запуск

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/mp_test             # юнит-тесты (headless) — должно быть ALL TESTS PASSED
```

## Быстрый старт (headless CLI)

В репозитории есть готовый демо-проект `examples/demo.mnp.json`
(зона V1: сенсор → ALIF-скрытый → мотор, Хебб-пластичность + записанный стенд):

```
./build/minosprime validate examples/demo.mnp.json              # флаги R1/8.x (exit 1 при красных)
./build/minosprime stands examples/demo.mnp.json                # регрессия стендов 9.4 (PASS/FAIL)
./build/minosprime serve examples/demo.mnp.json --port 18080    # API на 127.0.0.1 (Ctrl+C — стоп)
```

Пока `serve` работает (1 тик = 1 мс), можно дергать API из браузера или curl:

```
http://127.0.0.1:18080/get_schema
http://127.0.0.1:18080/set_sensor?id=V1.S.1&v=10
http://127.0.0.1:18080/get_neuron?id=V1.H.1
http://127.0.0.1:18080/stream?ids=V1.S.1,V1.H.1,V1.M.1&hz=10
```

## Проекты

Свою схему сохраняйте как JSON в формате `saveProjectJson` (magic «minosprime»),
файл с любым именем `.mnp.json`:

```
./build/minosprime validate my_project.mnp.json
```

## Ставка на C++17, внешних зависимостей нет.

## Формулы симуляции

Зафиксированы в `docs/SIMULATION.md`; v0.2 их не меняет (R3/12).
