# android-libcoro

Пример Android-проекта, интегрирующего [libcoro](external/libcoro) с возможностями networking + TLS и статически собранным OpenSSL.

## Требования
- Android SDK / NDK (ndkVersion задаётся в `app/build.gradle`)
- CMake / Ninja (через Android Gradle Plugin)
- Git submodules (обновить: `git submodule update --init --recursive`)

## OpenSSL
Скрипт `build_openssl.sh` собирает статические либы в `external/openssl/<ABI>`. Перед сборкой приложения убедитесь, что соответствующая директория существует (для каждого ABI, который включён в `abiFilters`).

## Запуск сборки
```
./gradlew :app:assembleDebug
```
APK появится в `app/build/outputs/apk/debug/`.

## Форматирование кода (clang-format)
В корне репозитория лежит файл `.clang-format` (базируется на LLVM стиле с кастомизациями). Все редактируемые / добавляемые C++ исходники должны быть прогнаны через `clang-format` до коммита.

### Быстрая проверка вручную
```
clang-format -i app/src/main/cpp/*.cpp
clang-format -i external/libcoro/include/coro/*.hpp
```
(Добавьте пути по необходимости.)

### Массовое автоформатирование
```
find app/src/main/cpp -name "*.c*" -o -name "*.h*" | xargs clang-format -i
find external/libcoro/include/coro -name "*.hpp" | xargs clang-format -i
```

### Git hook (опционально)
Создайте `.git/hooks/pre-commit`:
```
#!/usr/bin/env bash
CHANGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|cc|cpp|cxx|h|hpp)$' || true)
[ -z "$CHANGED" ] && exit 0
for f in $CHANGED; do
  [ -f "$f" ] || continue
  clang-format -i "$f"
  git add "$f"
done
```
Сделайте его исполняемым:
```
chmod +x .git/hooks/pre-commit
```

## Логирование
Используются макросы `LOGE`/`LOGI` в `main.cpp`. При необходимости вынесите их в отдельный заголовок `log.hpp`.

## Подсветка IntelliSense
Для корректной подсветки условно скомпилированного кода можно сгенерировать `compile_commands.json` (добавьте `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` в конфигурацию CMake) и указать путь в настройках VS Code C/C++.

## Лицензии
Сторонние компоненты смотрите в их директориях (`external/libcoro`, `external/libcoro/vendor/c-ares`, OpenSSL).
