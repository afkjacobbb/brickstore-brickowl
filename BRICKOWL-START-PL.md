# BrickOwl w BrickStore — prototyp natywny

Rozszerzenie istniejącego BrickStore (C++/Qt). Paczka zawiera kod źródłowy i patch;
nie jest instalatorem Windows. Instrukcje kompilacji platformowej pozostają takie
jak w oryginalnym projekcie. Sprawdzono kompilację na Linuxie z Qt 6.4.2.

## Uruchomienie

1. Skompiluj źródła zgodnie z README projektu; przykład CMake jest w
   `doc/development/brickowl.md`.
2. W Settings → BrickOwl wpisz klucz API, wykonaj Test connection i zapisz OK.
3. File → Import → BrickOwl Store Inventory pobiera oferty do zwykłego dokumentu.
   Podaj rzeczywistą walutę sklepu. Nierozpoznane pozycje są raportowane.
4. Dla synchronizacji obu sklepów uzupełnij cztery dane OAuth w Settings → BrickLink
   → BrickLink Store API. Dotychczasowy token BrickStore jest oddzielny.
5. Otwórz dokument z zapasem, następnie File → Export → Synchronize BrickLink ↔ BrickOwl.
6. Wybierz źródło docelowych ilości: dokument, BrickLink lub BrickOwl. Pobierz podgląd.
   Sprawdź dopasowania i zaznaczone wiersze. Tworzenie brakujących ofert BO jest
   opcjonalne i ograniczone do nowych części z jednoznacznym mapowaniem.
7. Wstrzymaj sprzedaż w obu sklepach, potwierdź to w oknie i zastosuj zmiany.
   Sprawdź wynik weryfikacji. Zapisz BSX, aby zachować identyfikatory ofert BO.

Nie wykonywano żądań na rzeczywistych kontach: nie przekazano poświadczeń.
Pierwszy test API należy przeprowadzić na kilku kontrolowanych ofertach przy
wstrzymanej sprzedaży. Cofnięcie zmian dokumentu nie cofa zmian sklepu.
Automatyczne rozliczanie sprzedaży pozostaje kolejnym etapem.

## Raport wymaganych 11 punktów

1. Przeanalizowane klasy: Core, Store, IO, Lot, Transfer, Config,
   CredentialsManager, DocumentIO, DocumentModel, Document, ActionManager,
   MainWindow i SettingsDialog — szczegóły w `doc/development/brickowl.md`.
2. Dotychczasowy BrickLink pobiera magazyn przez uwierzytelniony eksport XML;
   eksport aktualizacji również używa XML. Nowy klient OAuth obsługuje tylko
   bezpośrednie wywołania potrzebne ręcznej synchronizacji.
3. Zmodyfikowane pliki: lot.h/cpp, documentio.cpp, config.h/cpp, actionmanager.cpp,
   mainwindow.cpp, settingsdialog.h/cpp, CMakeLists.txt modułów common/desktop,
   BrickStoreXML.rnc.
4. Nowe pliki: common/brickowl.h/cpp, common/bricklinkstoreapi.h/cpp,
   desktop/brickowldialog.h/cpp, tests/brickowl/CMakeLists.txt i tst_brickowl.cpp,
   niniejsza instrukcja i dokumentacja w doc/development.
5. UI: ustawienia, import magazynu i okno podglądu/ręcznej synchronizacji.
6. Uwierzytelnianie BO: klucz API przechowywany przez istniejący CredentialsManager;
   GET przekazuje key w zapytaniu, POST w formularzu.
7. Pobieranie: inventory/list, katalog i kolory; konwersja do istniejącego Lot.
8. Mapowanie: oficjalne odwołanie bl_item_no, typ, odwołania kolorów bl_ids i stan;
   osobne BOID oraz identyfikatory ofert. Niejednoznaczne dopasowania blokowane.
9. Synchronizacja: podgląd, jawne źródło ilości, kontrola świeżości, sekwencyjne
   zmiany względne, ponowny odczyt i raport częściowych błędów.
10. BO: inventory/list, catalog/lookup, catalog/id_lookup, catalog/color_list,
    inventory/create, inventory/update i inventory/delete (ostatnia tylko w kliencie,
    bez przycisku usuwania). Szczegóły parametrów w dokumentacji technicznej.
11. Automatyzacja sprzedaży wymaga pobierania zamówień, trwałego rejestru zdarzeń,
    deduplikacji, obsługi anulowań i zmian, limitów API oraz rozwiązywania konfliktów.

Wyniki faktycznie wykonanych kontroli: `doc/development/VALIDATION.md`.
