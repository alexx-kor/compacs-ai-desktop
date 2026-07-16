# Сборка COMPACS Desktop для заказчика

Комплект из **3 ZIP-архивов** (каждый < 2 ГБ).

| Часть | Файл | Содержание |
|-------|------|------------|
| **1** | `COMPACS-Desktop-Part1-App.zip` | main.exe, config, vectors.bin, lemma_map, llama, START.cmd |
| **2** | `COMPACS-Desktop-Part2-ChatModel.zip` | llama3.2-3b-instruct-q4_K_M.gguf (~2 GB) |
| **3** | `COMPACS-Desktop-Part3-EmbedModel.zip` | nomic-embed-text.gguf (~260 MB) |

Документация: **RAG_ARCHITECTURE.md** (архитектура RAG), **qa_evaluation.json** (качество ~55%).

---

## Сборка (5 минут)

### 1. Распакуйте Part1

Например в `D:\COMPACS-Desktop\`:

```text
D:\COMPACS-Desktop\
  main.exe
  config.yaml
  vectors.bin
  lemma_map.tsv
  llama\llama-server.exe
  assets\index.html
  START.cmd          ← консольный режим (рекомендуется)
  START_UI.cmd       ← окно WebView (опционально)
  RAG_ARCHITECTURE.md
  qa_evaluation.json
  models\            (пусто)
```

### 2. Распакуйте Part2 и Part3 в ту же папку

```text
D:\COMPACS-Desktop\models\llama3.2-3b-instruct-q4_K_M.gguf
D:\COMPACS-Desktop\models\nomic-embed-text.gguf
```

### 3. Запуск

```bat
cd /d D:\COMPACS-Desktop
START.cmd
```

Откроется консоль. Введите вопрос после `> `. Первый ответ — 30–60 сек на CPU.

**WebView UI** (опционально): `START_UI.cmd` → http://127.0.0.1:8765

### 4. Требования

- Windows 10/11 x64
- WebView2 (только для START_UI)
- ~2.5 ГБ свободного места
- Интернет **не нужен** (полностью офлайн)

---

## Порты

| Порт | Назначение |
|------|------------|
| 8081 | llama embed |
| 8082 | llama chat |
| 8765 | UI (только START_UI) |

---

## Упаковка (разработчик)

```powershell
powershell -ExecutionPolicy Bypass -File .\pack_parts.ps1
```

Результат: `dist\COMPACS-Desktop-Part1-App.zip` и Part2/Part3.
