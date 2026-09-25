# Build Windows z integracją BrickOwl

Gotowa konfiguracja jest w `.github/workflows/build_cmake.yml` — rozszerza istniejący
job Windows. Uruchom w repozytorium GitHub Actions → CMake Build Matrix → Run workflow
z zaznaczonym `brickowl_prototype` (lub push na gałąź `brickowl-prototype`).

Wynik udanego uruchomienia: artifact **BrickStore-BrickOwl-Windows-x64** zawierający
**BrickStore-BrickOwl-Setup-x64.exe** i **BrickStore-BrickOwl-Windows-x64.zip**.
Nie wymaga lokalnej instalacji Qt, Visual Studio ani kompilowania przez użytkownika.

W obecnej sesji nie było Windows/MSVC ani połączonego repozytorium, więc CI nie
zostało uruchomione. Ta paczka jest konfiguracją automatycznego buildu, nie binarką.
Pełny opis i zakres testów: `doc/development/WINDOWS-PROTOTYPE-PL.md`.
