# Garibaldka

Gra karciana **Garibaldka** (Russian Bank / crapette) dla dwóch graczy: Ty kontra komputer.
C++17 / Win32, grafika **Direct2D + DirectWrite + WIC**, dźwięk **DirectSound** – te same techniki,
karty, dźwięki i animacje (rozdawanie, obrót, przesuwanie kart, fajerwerki) co w Pasjansie Dziadkowym.

## Uruchamianie

`Garibaldi.exe` – jeden plik, karty, ikony i dźwięki są wbudowane. Obok pliku powstaje `Garibaldi.ini`
(poziom, dźwięk, położenie okna) i folder `Sounds` – wrzucone tam pliki WAV o nazwach
`nowa`, `click`, `sukces`, `koniec`, `nono`, `podpowiedz` zastępują wbudowane dźwięki.

## Animacje

Rozdanie: obie talie leżą na jednej kupce, rozjeżdżają się na czerwoną i niebieską, potem karty lecą po kolei: czerwone na magazyn, niebieskie na magazyn, kolumny, czerwone do ręki, niebieskie do ręki. Na końcu równocześnie odwracają się wierzchnie karty magazynów i błyska karta, która rozstrzyga, kto zaczyna (200 ms, z poświatą na jedną kartę dookoła).

Każdy ruch karty przyspiesza w pierwszej połowie i zwalnia w drugiej. Odkrycie karty w magazynie następuje dopiero po zakończeniu ruchu karty, która ją odsłoniła. Karta, która ma się przy tym odwrócić (dobranie z talii, rozdanie, powrót pod talię), unosi się i obraca w środkowej części lotu.

## Sterowanie

* **Kliknij kartę** – przeniesie się automatycznie na najlepsze miejsce (fundament, kolumna...). Dobraną kartę, jeśli nic lepszego nie ma, odrzuci na śmietnik (kończy turę). Możesz też przeciągnąć kartę, gdzie chcesz.
* **Sekwens:** możesz złapać (przeciągnąć lub kliknąć) kartę w środku kolumny, jeśli od niej w dół leży ułożony ciąg (malejąco, na przemian kolory). Gdy da się go przenieść kolejnymi pojedynczymi ruchami (wolne kolumny, miejsca na wierzchach innych kolumn), gra znajduje najkrótszy plan i wykonuje go z animacją każdego ruchu. Gdy się nie da: „Za mało wolnego miejsca”.
* Kliknij swoją talię (lub spacja), aby dobrać kartę. Odrzucanie: przycisk „Odrzuć” lub klawisz D.
* Ścisły przymus: karta pasująca na fundament musi tam trafić. Kto spróbuje dobrać, odrzucić lub spasować, mając taką kartę, traci turę.
* Kto zaczyna: starsza karta w magazynie (animacja unoszenia, dwóch obrotów i błysku). Takie same figury: decyduje kolor (pik, kier, karo, trefl). Identyczne karty: każdy odkrywa pierwszą kartę z talii i ta rozstrzyga tak samo (figura, potem kolor), a gdy znów są identyczne, odkrywane są kolejne. Pokazane karty wracają pod talie.
* Fundamenty mają zarezerwowane kolory, po dwa na kolor w kolejności starszeństwa (pik, kier, karo, trefl); pusty fundament pokazuje 75% przezroczystego asa swojego koloru.
* Wygrywa ten, kto pierwszy pozbędzie się wszystkich kart (magazyn, talia i śmietnik).
* Gra zapisuje się przy wyjściu (plik `Garibaldi.sav`) i wczytuje przy następnym uruchomieniu.
* **Cofnij** (przycisk, `U`, `Ctrl+Z` lub `Backspace`): cofa Twoją ostatnią czynność (ruch, dobranie, odrzucenie, przeniesienie sekwensu jako jeden krok). Jeśli ta czynność skończyła turę, cofa też ruchy komputera wykonane od tamtej pory.
* `H` – podpowiedź (karta sama pokazuje ruch, raz), `F2` – nowa gra, `M` – dźwięk, `F1` – zasady.
* Poziom komputera zmieniasz przyciskiem „Poziom”. Zasady SI: [AI_RULES.md](AI_RULES.md).
* Diagnostyka: `F9` zapisuje stan stołu i dziennik ruchów do `garibaldi_dump.txt`, `F10` wymusza wygraną (test ekranu końca gry), `F11` zaczyna grę z identycznymi kartami w magazynach (test rozstrzygania z talii).

## Budowanie ze źródeł

```
pip install ziglang
powershell -ExecutionPolicy Bypass -File build.ps1
```

Wynik: `build/Garibaldi.exe`. Test silnika bez grafiki (komputer kontra komputer):
`python -m ziglang c++ -std=c++17 -O2 tools/sim.cpp -o build/sim.exe` i `build/sim.exe 600 2 2`. Testy reguł: `tools/test_rules.cpp`.

## Układ kodu

* `src/game.h` – reguły i SI (bez zależności od Windows).
* `src/main.cpp` – okno, rysowanie, animacje, obsługa myszy i klawiatury.
* `src/renderer_d2d.h`, `src/card_images_d2d.h`, `src/sound.h`, `src/anim.h` – z Pasjansa Dziadkowego.
* `res/` – karty PNG, ikony, dźwięki (z Pasjansa Dziadkowego).

Licencja kodu przejętego z Pasjansa Dziadkowego: MIT (plik `LICENSE`).
