# Сборка COMPACS Desktop из частей (< 2 ГБ каждая)

Проект разбит на **3 части**, чтобы каждая была меньше 2 ГБ (ограничение носителей/обмена).

| Часть | Архив | Содержание | ~размер |
|-------|--------|------------|---------|
| **1** | `COMPACS-Desktop-Part1-App.zip` | Приложение, UI, конфиг, vectors.bin, llama-engine | ~150–200 МБ |
| **2** | `COMPACS-Desktop-Part2-ChatModel.zip` | Модель генерации `llama3.2-3b-instruct-q4_K_M.gguf` | ~1.9 ГБ |
| **3** | `COMPACS-Desktop-Part3-EmbedModel.zip` | Модель эмбеддингов `nomic-embed-text.gguf` | ~260 МБ |

Скрипт упаковки: `pack_parts.ps1` (создаёт папку `dist\`).
Инструкция (этот файл): `ASSEMBLE.md`.

---

## Что нужно перед сборкой

- Windows 10/11 x64
- WebView2 Runtime (обычно уже есть с Microsoft Edge)
- Все три архива Part1–Part3 в одной папке
- Свободно **~2.5 ГБ** на диске после распаковки

---

## Сборка в одну папку (5 минут)

### Шаг 1 — Part1 (приложение)

1. Создайте целевую папку, например `D:\COMPACS-Desktop`.
2. Распакуйте в неё **целиком** `COMPACS-Desktop-Part1-App.zip`.

Должна получиться структура:

```text
D:\COMPACS-Desktop\
  main.exe
  export_vectors.exe          (опционально)
  config.yaml
  vectors.bin
  assets\index.html
  llama\llama-server.exe + DLL
  models\                     (пусто, кроме README)
  START.cmd
  STOP.cmd
  README.md
  ASSEMBLE.md
```

### Шаг 2 — Part2 (чат-модель)

Распакуйте `COMPACS-Desktop-Part2-ChatModel.zip` **в ту же** `D:\COMPACS-Desktop`.

Ожидаемый путь файла:

```text
D:\COMPACS-Desktop\models\llama3.2-3b-instruct-q4_K_M.gguf
```

### Шаг 3 — Part3 (эмбеддер)

Распакуйте `COMPACS-Desktop-Part3-EmbedModel.zip` **в ту же** `D:\COMPACS-Desktop`.

Ожидаемый путь файла:

```text
D:\COMPACS-Desktop\models\nomic-embed-text.gguf
```

### Шаг 4 — проверка состава

Убедитесь, что оба GGUF лежат именно в `models\`:

```bat
dir D:\COMPACS-Desktop\models
```

Должны быть видны оба файла (~1.9 ГБ + ~260 МБ).

### Шаг 5 — запуск llama-server

В отдельном окне (или сервисе) запустите llama-server на **127.0.0.1:8081** с обеими моделями
(один процесс: embed + completion), например:

```bat
cd /d D:\COMPACS-Desktop\llama
llama-server.exe -m ..\models\llama3.2-3b-instruct-q4_K_M.gguf --embedding --port 8081 -c 8192
```

Точные флаги зависят от вашей версии llama.cpp; главное — API на `http://127.0.0.1:8081` (`/embedding` или `/v1/embeddings` и `/completion`).

В `config.yaml` уже задано:

```yaml
server:
  host: 127.0.0.1
  port: 8081
```

### Шаг 6 — запуск приложения

```bat
cd /d D:\COMPACS-Desktop
START.cmd
```

или двойной клик по `main.exe`.

Окно **COMPACS RAG**, в статусе — число чанков (не «API недоступен»).  
UI в браузере: http://127.0.0.1:8765

Остановка: `STOP.cmd` или закрытие окна.

---

## Краткая схема

```text
Part1 ──► D:\COMPACS-Desktop\          (exe, assets, llama, config, vectors)
Part2 ──► D:\COMPACS-Desktop\models\   (chat .gguf)
Part3 ──► D:\COMPACS-Desktop\models\   (embed .gguf)
          │
          ▼
     llama-server :8081  +  main.exe :8765
```

---

## Типичные ошибки

| Проблема | Что проверить |
|----------|----------------|
| `vector store is empty` / magic | Есть ли `vectors.bin` рядом с `main.exe` (Part1) |
| «API недоступен» | Запущен ли `main.exe` (не открывать `index.html` как файл) |
| embed / ask не отвечает | llama-server на **8081**, пути к моделям из Part2/Part3 |
| Нет файла в `models\` | Архивы Part2/Part3 распакованы **в корень** комплекта, не «внутрь models\models» |

Если при распаковке Part2/Part3 получилось `models\models\*.gguf` — перенесите файлы на уровень выше:

```bat
move D:\COMPACS-Desktop\models\models\*.gguf D:\COMPACS-Desktop\models\
rmdir D:\COMPACS-Desktop\models\models
```

---

## Как пересобрать архивы частей (разработчику)

Из корня репозитория:

```powershell
powershell -ExecutionPolicy Bypass -File .\pack_parts.ps1
```

Результат в `dist\`:

- `COMPACS-Desktop-Part1-App.zip`
- `COMPACS-Desktop-Part2-ChatModel.zip`
- `COMPACS-Desktop-Part3-EmbedModel.zip`
- `ASSEMBLE.md` (копия этой инструкции)

Архивы с моделями большие — упаковка может занять несколько минут.
