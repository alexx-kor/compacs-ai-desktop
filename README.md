# COMPACS RAG Desktop

Автономное настольное приложение (Windows) для ответов по документации.
Работает полностью офлайн: векторная база + локальные модели. Без Python,
Docker, Ollama и внешних API.

## Быстрый старт

1. Распакуйте архив в любую папку (например `D:\COMPACS-RAG`).
2. Запустите **`START.cmd`** (или сразу `main.exe`).
3. Дождитесь окна **COMPACS RAG** и статуса «Готово | чанков в базе: …».
4. Введите вопрос и нажмите «Спросить». Браузер не нужен.

Первый запуск занимает 1–2 минуты — модели загружаются в память.
Остановка: **`STOP.cmd`**.

## Состав папки

| Путь | Назначение |
|------|------------|
| `main.exe` / `compacs-rag.exe` | Приложение: интерфейс + RAG + поиск по базе |
| `llama\` | Движок llama.cpp (`llama-server.exe` + DLL) |
| `models\nomic-embed-text.gguf` | Модель эмбеддингов (768d) |
| `models\llama3.2-3b-instruct-q4_K_M.gguf` | Модель генерации ответа |
| `rag\vectors.bin` | Векторы чанков (float32, 768d) |
| `rag\chunks.bin` | Метаданные чанков |
| `rag\texts.txt` | Тексты чанков |
| `rag\meta.json` | Описание индекса (count, dim, источники) |
| `assets\index.html` | Интерфейс чата |
| `START.cmd` / `STOP.cmd` | Запуск / остановка |
| `COMPACS-RAG-Desktop-Документация.docx` | Полная инструкция |

Текущая база: **2272 чанка**, размерность **768**.

## Порты (только localhost)

| Порт | Сервис |
|------|--------|
| 13081 | UI/API `main.exe` |
| 8081 | эмбеддинги (llama-server) |
| 8082 | генерация ответа (llama-server) |

## Требования

- Windows 10/11 x64
- WebView2 Runtime (обычно уже есть вместе с Microsoft Edge)
- ~2.5 GB свободного места

## Как это работает

1. Вопрос → эмбеддинг через llama-server (nomic-embed-text).
2. Косинусный поиск по `rag\vectors.bin` → топ похожих чанков.
3. Найденный контекст + вопрос → модель Llama 3.2 3B → ответ в окне.

## Добавить больше чанков (на стенде)

Переэкспорт готового индекса в десктоп:

```bat
python D:\compacs-ai-engine-develop\scripts\export_rag_index.py ^
  --input D:\compacs-ai-engine-develop\data\vectors_ollama768\chunks.json ^
  --output D:\COMPACS-RAG-Desktop-Portable\rag
```

Полная пересборка архива (сам выбирает самый большой индекс):

```bat
python D:\compacs-desktop\pack_portable.py
```

## Параллельно со стендом (3080 / 8080)

Десктоп embed слушает **8081**, engine стенда — **8080**. Конфликта нет.

| Сервис | Порт | Кто слушает |
|--------|------|-------------|
| RAG gateway (стенд) | 3080 | `python -m app serve` |
| RAG engine (стенд) | 8080 | тот же процесс |
| llama embed (десктоп) | 8081 | `compacs-rag.exe` |
| llama chat (десктоп) | 8082 | `compacs-rag.exe` |

## Диагностика

| Симптом | Причина | Решение |
|---------|---------|---------|
| HTTP 404 / embed не работает | Занят порт 8081 или старая сборка | Перезапустить `START.cmd`, обновить exe |
| «Failed to fetch» / API недоступен | Открыт `index.html` в браузере | Запускать только `main.exe` / `START.cmd` |
| Окно не открывается | Нет WebView2 | Установить WebView2 Runtime |
| `cannot parse embedding response` | Запущена старая сборка | Обновить `main.exe`, перезапустить |
| `missing llama\...dll` | Неполная распаковка | Распаковать архив целиком |
| Долгая загрузка | Большие модели | Подождать 1–2 минуты после старта |
