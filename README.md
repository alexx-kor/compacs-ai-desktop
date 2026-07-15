# COMPACS RAG Desktop

Автономное настольное приложение (Windows) для ответов по документации.
Работает офлайн: векторная база `vectors.bin` (формат COMPACS1) + локальный
`llama-server`. Без Python runtime, Docker, Ollama и внешних API.

## Быстрый старт (готовая сборка)

1. Соберите проект (`build.bat`) или возьмите уже собранный `build\Release\`.
2. Рядом с `main.exe` должны лежать:
   - `config.yaml`
   - `vectors.bin` (COMPACS1)
   - `assets\index.html`
3. Запустите `llama-server` на `127.0.0.1:8081` (embed + completion в одном процессе).
4. Запустите **`build\Release\main.exe`**.
5. Откроется окно **COMPACS RAG**. В статус-баре должно быть число чанков и URL llama.
6. Задайте вопрос («Спросить»). UI также доступен в браузере: http://127.0.0.1:8765

Остановка: закройте окно приложения. Скрипт `STOP.cmd` — для остановки связанных процессов (если используете).

### Раздача частями (< 2 ГБ)

Из‑за размера GGUF комплект делится на 3 архива. Инструкция: **[ASSEMBLE.md](ASSEMBLE.md)**.  
Упаковка: `powershell -ExecutionPolicy Bypass -File .\pack_parts.ps1` → папка `dist\`.

## Сборка из исходников

```bat
build.bat
```

Артефакты:

| Файл | Назначение |
|------|------------|
| `build\Release\main.exe` | Desktop: WebView UI + локальный HTTP API + RAG |
| `build\Release\export_vectors.exe` | Конвертер `chunks.json` → `vectors.bin` (COMPACS1) |

Требования для сборки:

- Visual Studio 2022 Build Tools (x64)
- CMake (из VS)
- WebView2 SDK в `.tools\mswebview2` (или `COMPACS_WEBVIEW2_DIR`)

## Состав репозитория / рабочей папки

| Путь | Назначение |
|------|------------|
| `main.cpp`, `config.cpp` / `config.hpp` | Приложение и runtime-конфиг |
| `format_vectors.hpp` | Единая схема формата COMPACS1 (reader + writer) |
| `tools\export_vectors\` | Утилита экспорта векторов |
| `assets\index.html` | UI (fetch к локальному API) |
| `config.yaml` | Шаблон runtime-конфига (копируется рядом с exe) |
| `chunks.json` | Исходные чанки + embeddings (не в git) |
| `build\Release\` | Собранные exe и runtime-комплект |
| `llama\` | Движок llama.cpp (`llama-server.exe` + DLL) — не в git |
| `models\` | GGUF-модели — не в git |
| `build.bat` | Configure + Release build |

## Конфигурация (`config.yaml`)

Приоритет: **env > YAML > значения по умолчанию в коде**.

Основные ключи:

| Ключ | Смысл (шаблон) |
|------|----------------|
| `server.port` | **8081** — рабочий порт llama-server (embed+completion) |
| `server.gen_port` / `embed_port` | reserved (8082 / 8081), launcher |
| `ui.port` | **8765** — локальный UI/API (`127.0.0.1`, host не меняется) |
| `retrieval.vector_store` | путь к `vectors.bin` относительно exe |
| `retrieval.similarity_threshold` | cosine **distance** (keep if distance < threshold) |

Переопределение через env:

- `COMPACS_LLAMA_SERVER_URL` (например `http://127.0.0.1:8081`)
- `COMPACS_EMBED_MODEL`
- `COMPACS_UI_PORT`

При первом запуске без `config.yaml` рядом с exe создаётся starter-файл (stand-aligned значения); **текущая сессия** всё равно использует code defaults до рестарта.

В логе при старте печатается блок `=== COMPACS config (effective) ===`.

## Порты (localhost)

| Порт | Сервис |
|------|--------|
| **8765** | UI + API `main.exe` (`/health`, `/api/info`, `/api/ask`) |
| **8081** | llama-server (embedding + completion) |

`gen_port` / `embed_port` в YAML — reserved под launcher; текущий `main.exe` ходит на `server.host`:`server.port`.

## Векторная база (COMPACS1)

Формат описан в `format_vectors.hpp` / читается в `main.cpp`:

- magic `"COMPACS1"`, version `1`, dim обычно **768**, float32 LE
- записи: `id`, `page`, `source`/`text` (UTF-8) + embedding

Конвертация из `chunks.json` (поле `chunk` или `text` + `embedding`):

```bat
build\Release\export_vectors.exe chunks.json build\Release\vectors.bin
```

Статистика: число записей, dim, размер файла. При `embedding` ≠ 768 — ошибка с номером строки, без пропуска.

`rag\vectors.bin` (magic `RAGVEC01`) — формат стенда/mcp-layer; desktop его **не** читает.

Текущий экспорт из `chunks.json`: **131** чанк, dim **768**.

## Как это работает

1. UI (`assets\index.html`) обращается к `http://127.0.0.1:<ui.port>` (`window.COMPACS_API_BASE`).
2. Вопрос → эмбеддинг через llama-server на `server.port`.
3. Косинусный поиск по `vectors.bin` → топ чанков по `similarity_threshold`.
4. Контекст + вопрос → `/completion` на том же llama-server → ответ (+ sources).

## API (для проверки)

```bat
curl http://127.0.0.1:8765/health
curl http://127.0.0.1:8765/api/info
curl -X POST http://127.0.0.1:8765/api/ask -H "Content-Type: application/json" --data-binary @ask.json
```

Пример `ask.json`:

```json
{"question":"Что показывает экран Монитор?","stream":false}
```

## Параллельно со стендом

| Сервис | Порт |
|--------|------|
| RAG gateway (стенд) | 3080 |
| RAG engine (стенд) | 8080 |
| llama-server (desktop) | **8081** |
| desktop UI/API | **8765** |

Десктоп не занимает 8080/3080 — конфликта со стендом нет.

## Диагностика

| Симптом | Причина | Решение |
|---------|---------|---------|
| `null/api/ask` / «API недоступен» | UI не на HTTP (`file://`) или старый exe | Запускать `build\Release\main.exe`, не открывать `index.html` как файл |
| `invalid vectors magic` | Файл не COMPACS1 (например `RAGVEC01`) | Пересобрать через `export_vectors.exe` из `chunks.json` |
| `vector store is empty` | Нет / пустой `vectors.bin` рядом с exe | Скопировать/экспортировать `vectors.bin` в `build\Release\` |
| embed / ask зависает | llama-server на 8081 не отвечает | Перезапустить llama-server; проверить `llama_base_url` в effective config |
| Окно не открывается | Нет WebView2 | Установить WebView2 Runtime |
| Порт 8765 занят | Предыдущий `main.exe` | Закрыть процесс или сменить `ui.port` / `COMPACS_UI_PORT` |

## Требования (runtime)

- Windows 10/11 x64
- WebView2 Runtime (обычно с Microsoft Edge)
- Запущенный `llama-server` на порту из `config.yaml` (`server.port`, по умолчанию шаблона — 8081)
- Модели GGUF рядом с llama-server (см. `models\` в шаблоне конфига)
