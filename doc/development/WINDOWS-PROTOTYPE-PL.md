# BrickStore BrickOwl Prototype — Windows x64

## Status tej paczki

W tej sesji dostępny był Linux, bez MSVC i bez połączonego repozytorium GitHub.
Nie wygenerowano ani nie uruchomiono binarki Windows. Dodano kompletną ścieżkę
Windows do istniejącego `.github/workflows/build_cmake.yml`. Wynik wykonania CI
musi potwierdzić poprawność buildu MSVC, instalacji i uruchomienia na Windows.
Nie należy traktować pliku ZIP z konfiguracją CI jako wersji portable aplikacji.

## Uzyskanie plików — bez lokalnej kompilacji

Zmiany muszą znaleźć się w Twoim repozytorium/forku GitHub (razem z całą integracją
BrickOwl). Następnie otwórz Actions → CMake Build Matrix → Run workflow i pozostaw
włączone `brickowl_prototype`. Workflow jest też uruchamiany automatycznie po pushu
na gałąź `brickowl-prototype`. Nie wymaga kluczy API sklepów ani sekretów publikacji.

Po zielonym wyniku pobierz artifact `BrickStore-BrickOwl-Windows-x64`. Zawiera:

- `BrickStore-BrickOwl-Setup-x64.exe` — instalator Inno Setup;
- `BrickStore-BrickOwl-Windows-x64.zip` — program i biblioteki;
- `SHA256SUMS.txt` — sumy kontrolne obu plików.

Oddzielny artifact `BrickStore-BrickOwl-Windows-smoke` zawiera raporty JSON,
log instalatora i zrzuty ekranu z testów obu sposobów uruchomienia.
W przypadku nieudanego testu workflow nie publikuje paczki jako gotowego pobrania.

## Uruchomienie gotowego buildu

Instalator: uruchom EXE i wybierz katalog. Powstaje skrót Start oraz opcjonalny
skrót na pulpicie; deinstalacja jest dostępna w ustawieniach aplikacji Windows.
Portable: rozpakuj **cały** ZIP i uruchom `BrickStore.exe`. DLL pozostają obok EXE,
a pluginy Qt w podkatalogach. Nie przenoś samego EXE. Nie trzeba instalować Qt.
Ta odmiana portable oznacza samowystarczalną paczkę programu; ustawienia nadal
zapisują się w profilu użytkownika, nie na pendrivie.

Obsługiwany cel: Windows 10 od 1809 i Windows 11 x64. To nie jest build ARM64.
Instalator jest niepodpisanym buildem testowym, nie oficjalnym wydaniem BrickStore.
Prototyp ma osobną nazwę, AppId instalatora i ustawienia. Nie przejmuje skojarzenia
BSX i nie usuwa istniejącego BrickStore. Mechanizm poświadczeń pozostaje upstreamowy.

## Funkcje BrickOwl

Settings → BrickOwl: wpisanie klucza i test połączenia.
File → Import → BrickOwl Store Inventory: import do zwykłego dokumentu BrickStore.
File → Export → Synchronize BrickLink ↔ BrickOwl: porównanie, jawny wybór źródła
ilości, ręczne zastosowanie i ponowna weryfikacja obu sklepów. Oddzielne BOID,
identyfikatory ofert i mapowanie kolorów; niejednoznaczne wiersze są blokowane.
Opcjonalne tworzenie brakujących ofert BO obsługuje jednoznacznie mapowane nowe części.
Automatyczna obsługa sprzedaży nie jest zaimplementowana. Klucze rzeczywistych kont
nie są potrzebne do buildu ani testu CI; samych wywołań live nie sprawdzono.

## Wykorzystana infrastruktura

- Istniejący job `build-windows`, bez drugiego równoległego workflow.
- `windows-latest`, MSVC **2022** znajdowany przez vswhere, Ninja, `Release`.
- Qt **6.11.2**, `win64_msvc2022_64`, z istniejącej akcji `install-qt` i jej modułów.
- QCoro **0.12.0**, jak w CMake projektu.
- Oficjalny cel CMake `deploy` używa `windeployqt` z analizą QML.
- Oficjalny cel `installer` i `windows/brickstore.iss` tworzą EXE przez Inno Setup.
- `prepare-prototype.ps1` dodaje lokalny redystrybuowalny runtime MSVC i sprawdza
  obecność Qt/Windows/SVG/Schannel. TLS używa Schannel, zgodnie z istniejącą akcją
  usuwającą backend OpenSSL; nie wymaga zewnętrznej instalacji OpenSSL.
- `test-prototype.ps1` archiwizuje drzewo deploy, rozpakowuje ZIP, czyści PATH z Qt,
  uruchamia natywny test, instaluje EXE, ponawia test i sprawdza deinstalację.
- `windows_smoke.cpp` sprawdza widoczność okna i ustawień, edycję maskowanego klucza,
  akcje BrickLink/BrickOwl, otwarcie importu, parser, podpis OAuth, TLS i pluginy.
  Klucz testowy nie jest zapisywany; test nie wywołuje operacji magazynowych.
  Pomija pierwszorazowe prompty pobierania katalogu i aktualizacji (`afterInit`).
- Opcja `BRICKOWL_PROTOTYPE` jest domyślnie wyłączona w zwykłym CMake. W trybie
  prototypu workflow pomija inne platformy, wysyłanie do Sentry i publikację sklepów.

Test CI nie dowodzi poprawności wszystkich dotychczasowych funkcji BrickLink ani
synchronizacji rzeczywistych sklepów. Sprawdza obecność ich akcji i podstawowe UI.
