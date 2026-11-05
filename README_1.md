# nd_internal (Oxide Survival Island, dump 1_17_12000)

Wenzo Soft internal: ESP + аим камеры, неоновое фиолетово-пурпурное меню на ImGui (тач). Настройки — в меню.

## Сборка на GitHub
1. Создай репозиторий (можно private) -> Add file -> Upload files -> закинь ВСЁ содержимое этой папки,
   включая скрытую папку `.github` (если веб-загрузка её не берёт — залей через git, см. ниже).
2. Вкладка **Actions** -> workflow `build-nd-internal` -> Run workflow (или просто сделай push).
3. Когда job зелёный: открой run -> внизу **Artifacts** -> `libnd_internal-arm64-v8a` -> скачай zip -> внутри `libnd_internal.so`.

Через git:
    git init && git add -A && git commit -m init
    git branch -M main && git remote add origin https://github.com/<you>/<repo>.git && git push -u origin main

## Запуск в LuluBox (non-root)
1. Распакуй APK игры (apktool d game.apk), положи `libnd_internal.so` в `lib/arm64-v8a/`.
2. В smali главной активности (в manifest: `android.intent.action.MAIN`), в начало `onCreate`, после `super.onCreate`:
       const-string v0, "nd_internal"
       invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
   (если регистры заняты — увеличь `.locals`).
3. apktool b -> zipalign -> apksigner (свой ключ). Установи и добавь в LuluBox как обычную игру.
   Если у игры split APK (base + config) — либа кладётся в split `config.arm64_v8a`.
4. В игре: кнопка «W» слева сверху открывает/закрывает меню. Вкладки AIM / ESP / MISC.
   Аим по умолчанию работает, пока зажата экранная кнопка AIM (справа); можно отключить в меню.

## Проверка
    adb logcat -s nd
Ожидаемо: `resolve: ... esp=1 aim=1`, затем `hooked eglSwapBuffers`.
`aim=0` в логе при `esp=1` значит, что обфусцированные поля MouseLook не совпали с дампом (другая сборка).
Для проверки ролей кватернионов: MISC -> Log look quats, крути камеру, сверяй `waV`/`waI` в логе.
MISC также показывает Touch input n=… — при касании число должно меняться; если FAIL/0, тач через legacy Input не работает.

## Не проверено
Сборка ImGui/Dobby в CI, сборка либы, загрузка в процесс, корректность ESP/аима, работа через LuluBox.
FILL в internal.cpp: проверка «жив ли игрок».
