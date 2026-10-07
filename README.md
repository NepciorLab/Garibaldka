# Garibaldka

Gra karciana **Garibaldka** (Russian Bank / crapette) dla dwóch graczy: Ty kontra komputer.
C++17 / Win32, grafika **Direct2D + DirectWrite + WIC**, dźwięk **DirectSound** – te same techniki,
karty, dźwięki i animacje (rozdawanie, obrót, przesuwanie kart, fajerwerki) co w Pasjansie Dziadkowym.

## Uruchamianie

`Garibaldi.exe` – jeden plik, karty, ikony i dźwięki są wbudowane. Obok pliku powstaje `Garibaldi.ini`
(poziom, dźwięk, położenie okna) i folder `Sounds` – wrzucone tam pliki WAV o nazwach
`nowa`, `click`, `sukces`, `koniec`, `nono`, `podpowiedz`, `cofnij`, `plomien` zastępują wbudowane dźwięki.

## Animacje

Płomień obok magazynu pokazuje, czyja jest tura: zapala się po wyłonieniu zaczynającego gracza i przesuwa się (z przyspieszeniem i zwolnieniem) do drugiego gracza po zakończeniu tury. W rozdaniu karty na stół (kolumny) trafiają na samym końcu.

Rozdanie: obie talie leżą na jednej kupce, rozjeżdżają się na czerwoną i niebieską, potem karty lecą po kolei: czerwone na magazyn, niebieskie na magazyn, kolumny, czerwone do ręki, niebieskie do ręki. Na końcu równocześnie odwracają się wierzchnie karty magazynów i błyska karta, która rozstrzyga, kto zaczyna (200 ms, z poświatą na jedną kartę dookoła).

Każdy ruch karty przyspiesza w pierwszej połowie i zwalnia w drugiej. Odkrycie karty w magazynie następuje dopiero po zakończeniu ruchu karty, która ją odsłoniła. Karta, która ma się przy tym odwrócić (dobranie z talii, rozdanie, powrót pod talię), unosi się i obraca w środkowej części lotu.

## Sterowanie

* **Kliknij kartę** – przeniesie się na najlepszy z wszystkich dozwolonych ruchów (fundament, kolumna, także dołożenie na śmietnik lub magazyn przeciwnika). Dobraną kartę odrzuci na śmietnik tylko wtedy, gdy nie ma dla niej żadnego innego ruchu. Możesz też przeciągnąć kartę lub cały sekwens, gdzie chcesz.
* **Sekwens:** możesz złapać (przeciągnąć lub kliknąć) kartę w środku kolumny, jeśli od niej w dół leży ułożony ciąg (malejąco, na przemian kolory). Gdy da się go przenieść kolejnymi pojedynczymi ruchami (wolne kolumny, miejsca na wierzchach innych kolumn), gra znajduje najkrótszy plan i wykonuje go z animacją każdego ruchu. Gdy się nie da: „Za mało wolnego miejsca”.
* Kliknij swoją talię (lub spacja), aby dobrać kartę. Odrzucanie: przycisk „Odrzuć” lub klawisz D.
* Ścisły przymus: karta pasująca na fundament musi tam trafić. Kto spróbuje dobrać, odrzucić lub spasować, mając taką kartę, traci turę.
* Kto zaczyna: starsza karta w magazynie (animacja unoszenia, dwóch obrotów i błysku). Takie same figury: decyduje kolor (pik, kier, karo, trefl). Identyczne karty: każdy odkrywa pierwszą kartę z talii i ta rozstrzyga tak samo (figura, potem kolor), a gdy znów są identyczne, odkrywane są kolejne. Pokazane karty wracają pod talie.
* Fundamenty mają zarezerwowane kolory, po dwa na kolor w kolejności starszeństwa (pik, kier, karo, trefl); pusty fundament pokazuje 75% przezroczystego asa swojego koloru.
* Wygrywa ten, kto pierwszy pozbędzie się wszystkich kart (magazyn, talia i śmietnik).
* Gra zapisuje się przy wyjściu (plik `Garibaldi.sav`) i wczytuje przy następnym uruchomieniu.
* **Cofnij** (przycisk, `U`, `Ctrl+Z` lub `Backspace`): cofa Twoją ostatnią czynność (ruch, dobranie, odrzucenie, przeniesienie sekwensu jako jeden krok). Jeśli ta czynność skończyła turę, cofa też ruchy komputera wykonane od tamtej pory.
* `H` – podpowiedź (karta sama pokazuje ruch, raz), `F2` – nowa gra, `M` – dźwięk, `F1` – zasady (przewijane okno w stylu gry: kółko myszy, strzałki, PgUp/PgDn, Esc zamyka).
* Poziom komputera zmieniasz przyciskiem „Poziom”. Zasady SI: [AI_RULES.md](AI_RULES.md).
* Diagnostyka: `F9` zapisuje stan stołu i dziennik ruchów do `garibaldi_dump.txt`, `F10` wymusza wygraną (test ekranu końca gry), `F11` zaczyna grę z identycznymi kartami w magazynach (test rozstrzygania z talii).

## Gra sieciowa

Przycisk „Sieć” ma dwie zakładki. Każdy wybiera sobie nick. W grze sieciowej jest czat z emotkami i prośba o nową partię; nie ma cofania ani podpowiedzi.

- **Sieć lokalna**: jeden gracz klika „Hostuj grę” (gra pokazuje adres IP jego komputera, port 47321), drugi wpisuje ten adres i klika „Połącz”. Przy pierwszym hostowaniu Zapora Windows zapyta o zgodę.
- **Internet**: gra łączy się z serwerem (Cloudflare Workers, katalog `server/`). Jeden gracz klika „Utwórz pokój” i podaje znajomemu 4-znakowy kod, drugi wpisuje kod i klika „Dołącz”. Nick jest zastrzeżony na serwerze (klucz zapisany w pliku .ini), serwer prowadzi bilans spotkań dwóch graczy (wynik liczy się, gdy obaj zgłoszą ten sam). Dostęp tylko dla znajomych chroni hasło serwera.

Serwer: `cd server && npm install && npx wrangler deploy`, hasło: `npx wrangler secret put INVITE_CODE`. Test protokołu: `WS_URL=wss://.../ws INVITE=hasło node test/protocol.mjs`.

## Aktualizacje

Gra przy starcie (w tle) sprawdza w GitHub Releases tego repozytorium, czy jest nowsza wersja, tak samo jak Pasjans Dziadkowy. Jeśli jest, pyta o zgodę, pobiera `Garibaldi.exe`, podmienia plik i uruchamia się ponownie (zapisana gra zostaje). Sprawdzanie przy starcie można wyłączyć w oknie zasad. Numer wersji i data budowy są na początku okna zasad.

Aby opublikować nową wersję: zmień `APP_VERSION` w `src/main.cpp`, zbuduj `Garibaldi.exe`, utwórz Release z tagiem `vMAJOR.MINOR.PATCH` (zgodnym z `APP_VERSION`) i dołącz do niego plik dokładnie o nazwie `Garibaldi.exe`. Opis wydania jest pokazywany użytkownikom jako „Co nowego”.

## Budowanie ze źródeł

```
pip install ziglang
powershell -ExecutionPolicy Bypass -File build.ps1
```

Wynik: `build/Garibaldi.exe`. Test silnika bez grafiki (komputer kontra komputer):
`python -m ziglang c++ -std=c++17 -O2 tools/sim.cpp -o build/sim.exe` i `build/sim.exe 600 2 2`. Testy reguł: `tools/test_rules.cpp`.

`res/sounds/plomien.wav` (przesunięcie płomienia) powstał z `Pochodnia.mp3` narzędziem `tools/mp3_to_wav.cpp` (dekoder Windows Media Foundation).

## Układ kodu

* `src/game.h` – reguły i SI (bez zależności od Windows).
* `src/main.cpp` – okno, rysowanie, animacje, obsługa myszy i klawiatury.
* `src/renderer_d2d.h`, `src/card_images_d2d.h`, `src/sound.h`, `src/anim.h` – z Pasjansa Dziadkowego.
* `res/` – karty PNG, ikony, dźwięki (z Pasjansa Dziadkowego).

Licencja kodu przejętego z Pasjansa Dziadkowego: MIT (plik `LICENSE`).

## Zapis ruchów

Gra zapisuje każdą partię i każdy ruch (także komputera i przeciwnika w sieci) do pliku `garibaldka_ruchy.log` obok programu: kto, co zagrał, jakie ruchy były wtedy możliwe i pełny stan gry przed ruchem. Podejrzane decyzje są oznaczone słowem CHECK (np. odrzucenie karty, którą dało się zagrać). Plik nie rośnie ponad ok. 12 MB (starsza część trafia do `.old`).

## Ustawienia, hot seat, statystyki

- **Ustawienia** (F3): *Ogólne* (aktualizacje), *Rozgrywka* (poziom komputera, automatyczne ruchy, przymus fundamentu: „Karaj” albo „Przypomnij”), *Grafika* (na razie pusta), *Dźwięk* (głośność, własne dźwięki WAV/MP3 dla każdego zdarzenia), *Sterowanie* (własne skróty klawiszowe, po dwa na akcję). Wszystko, łącznie z ostatnio wybraną grupą, zapisuje się w pliku `.ini`.
- **Hot seat** (F7): dwóch graczy przy jednym komputerze (Gracz 1 na dole, Gracz 2 na górze). Cofnąć można ruchy z własnej tury. Takie partie nie są liczone w statystykach.
- **Statystyki** (F4): rozegrane i wygrane partie oraz % wygranych dla każdego poziomu komputera i dla gry przez sieć.
- **Graj przez sieć** (F5): hasło serwera jest wpisane domyślnie.

## Języki / Languages

Wybór języka: Ustawienia → Ogólne → Język. Domyślnie gra używa języka systemu (polski albo angielski). The language is chosen in Settings → General; by default it follows the system language.

Teksty są oddzielone od kodu. Językiem źródłowym jest polski: każdy tekst w kodzie to polski `msgid` w `T(L"...")`, a tłumaczenie to plik katalogu gettext (`.po`), po jednym na język. Angielski (`res/lang/en.po`) jest wbudowany w exe. The source language is Polish; every language is one gettext `.po` file (msgid = the Polish text, msgstr = the translation); English is built into the exe.

- **Nowy język bez przebudowy / a new language without rebuilding:** put an `xx.po` file into a `lang` folder next to `Garibaldi.exe` (same format; the header gives `"Language: xx\n"` and `"Language-Name: ...\n"`). It appears in the language list. A file `lang\en.po` overrides single English texts. Edit it with any text editor or with Poedit.
- **Teksty ze wstawkami / texts with values** are put together from fragments in the order of the translation (`{0}`, `{1}` in `i18n::fmt`).
- **Brakujące tłumaczenia / missing translations:** run the game with `/missing`; the texts without a translation go to `i18n_missing.txt`.
- **Dla programisty / for the developer:** `python tools/i18n_build.py` rebuilds `res/lang/en.po` from `tools/en_list.py` and checks that every text of the code has its translation; add a new text to the code and to the list in the same place in the order.
