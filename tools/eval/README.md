# tools/eval — приёмочные и диагностические скрипты

**Не входят в runtime и в dist.** Продукт — чистый C++ офлайн (`main.exe` + `llama-server`).
Python нужен только разработчику для Golden Set / замеров.

## Требования

- Уже запущенные `llama-server` на `:8081` (embed) и `:8082` (chat) — обычно через `START.cmd` / `START_UI.cmd` из корня репо.
- Собранный `build\Release\main.exe` (или `main.exe` рядом со скриптом запуска).
- Python 3.10+ (stdlib only, без `requirements.txt` / venv).

## Файлы

| Файл | Назначение |
|------|------------|
| `qa_evaluation.json` / `.csv` | Golden Set (эталонные ответы и страницы) |
| `run_qa_eval.py` | Прогон Golden Set через `main.exe -q` |
| `score_threshold.py` | Top-1 dense similarity / distance для подбора `similarity_threshold` |
| `bench_ask.py` | Контрольные тайминги (Монитор / синхронизация / OOD) |

## Запуск

Из корня репозитория (после `START_UI.cmd` или с уже поднятыми :8081/:8082):

```bat
python tools\eval\run_qa_eval.py
python tools\eval\score_threshold.py
python tools\eval\bench_ask.py
```

Опционально: `set COMPACS_MAIN=C:\path\to\main.exe` если exe не в `build\Release`.
