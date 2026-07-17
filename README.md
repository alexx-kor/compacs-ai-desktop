# COMPACS RAG Desktop

Автономное настольное приложение (Windows) для ответов по документации COMPACS.
Профиль **hybrid_dense_3b_cpu**: dense + BM25 + RRF, rerank, collection routing.
Работает офлайн: `vectors.bin` + два `llama-server` (embed :8081, chat :8082).
**Python / Docker / Ollama / внешние API не требуются** для запуска.

## Быстрый старт (готовый комплект)

1. Распакуйте Part1–Part3 по **[ASSEMBLE.md](ASSEMBLE.md)**.
2. Запустите **`START.cmd`** — поднимет llama на 8081/8082 и откроет консольный RAG.
3. Введите вопрос после `> `.
4. **WebView UI** (опционально): `START_UI.cmd` → http://127.0.0.1:8765

Остановка: `STOP.cmd`.

### Раздача частями (< 2 ГБ)

```powershell
powershell -ExecutionPolicy Bypass -File .\pack_parts.ps1
```

Результат в `dist\`: Part1 (приложение) + Part2 (чат-модель) + Part3 (эмбеддер).

## Сборка из исходников

```bat
build.bat
```

Артефакты в `build\Release\`:

| Файл | Назначение |
|------|------------|
| `main.exe` | RAG + опционально WebView UI (:8765) |
| `export_vectors.exe` | Конвертер `chunks.json` / `--from-rag` → COMPACS1 |
| `config.yaml`, `lemma_map.tsv`, `vectors.bin`, `assets\` | копируются post-build |

После `build.bat` запуск **одной командой** из корня репо: `START.cmd` (берёт `build\Release\main.exe`, модели из `models\`, llama из `llama\`). Ручное копирование exe/`vectors.bin` в корень не нужно.

## Режимы запуска

| Команда | Описание |
|---------|----------|
| `START.cmd` | llama :8081/:8082 + `main.exe --console` |
| `START_UI.cmd` | llama + WebView UI |
| `main.exe --console` | Консольный RAG (llama уже запущен) |
| `main.exe -q "вопрос"` | Один вопрос в stdout |

## Архитектура RAG

См. **[RAG_ARCHITECTURE.md](RAG_ARCHITECTURE.md)**.

Кратко: вопрос → collection routing → embed (:8081) → hybrid retrieve (dense+BM25+RRF) → lexical rerank → context → Llama 3 chat (:8082).

## Конфигурация (`config.yaml`)

Приоритет: **env > YAML > код**.

Ключевые параметры профиля (заморожены для десктопного CPU):

| Ключ | Значение |
|------|----------|
| `retrieval.hybrid_enabled` | true |
| `retrieval.top_k` / `rerank_top_k` | 12 / 8 |
| `retrieval.context_chunks` / `chunk_chars` | 3 / 1000 |
| `retrieval.similarity_threshold` | 0.206 (cosine distance) |
| `generation.num_ctx` / `num_predict` | 8192 / 200 |
| `server.embed_port` / `gen_port` | 8081 / 8082 |

## Порты

| Порт | Сервис |
|------|--------|
| 8081 | llama embed (`nomic-embed-text.gguf`) |
| 8082 | llama chat (`llama3.2-3b-instruct-q4_K_M.gguf`) |
| 8765 | UI/API `main.exe` (только START_UI) |

## Векторная база

- Формат COMPACS1: magic `COMPACS1`, dim 768, float32 — см. `format_vectors.hpp`.
- Полный индекс: **2272** чанка.

```bat
build\Release\export_vectors.exe --from-rag rag build\Release\vectors.bin
```

## Приёмочные скрипты (dev-only, Python)

Golden Set и замеры живут в **`tools/eval/`** (не входят в dist). См. `tools/eval/README.md`.

```bat
python tools\eval\run_qa_eval.py
python tools\eval\bench_ask.py
```

## API

```bat
curl http://127.0.0.1:8765/health
curl http://127.0.0.1:8765/api/info
curl -X POST http://127.0.0.1:8765/api/ask -H "Content-Type: application/json" -d "{\"question\":\"Что показывает экран Монитор?\",\"stream\":false}"
```

## Диагностика

| Симптом | Решение |
|---------|---------|
| `lemma_map not found` | Рядом с exe должен быть `lemma_map.tsv` (post-build копирует) |
| embed / ask зависает | `STOP.cmd`, затем `START.cmd` |
| `invalid vectors magic` | Пересобрать через `export_vectors.exe` |
| Пустые ответы | Проверить llama :8081 и :8082 (`/health`) |

## Требования

- Windows 10/11 x64
- WebView2 Runtime (для START_UI)
- VS 2022 Build Tools + CMake (для сборки)
- ~2.5 ГБ на диске (модели + индекс)
- Python **не** нужен для runtime
