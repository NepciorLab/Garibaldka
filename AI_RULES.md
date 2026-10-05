# Jak gra komputer – 10 zasad SI

Komputer w każdej chwili wylicza wszystkie dozwolone ruchy (`legalMoves` w `src/game.h`), ocenia je punktami
(`scoreMove`) i wykonuje najlepszy. **Ruch bez żadnej korzyści (ocena 0 lub mniej) nie jest ani wykonywany,
ani podpowiadany**, np. odłożenie karty z kolumny na magazyn czy śmietnik przeciwnika, które niczego nie odsłania,
nie zwalnia kolumny i niczego nie blokuje. Losowy szum służy tylko do wyboru między ruchami, które mają sens.
Gdy żaden ruch nie ma dodatniej oceny, komputer dobiera kartę z talii, a jeśli dobrana karta nic nie daje,
odrzuca ją na śmietnik (koniec tury).

Obowiązek (zasada 1) jest sprawdzany przed wszystkim innym i na każdym poziomie. Obowiązuje ścisły przymus:
każda karta, którą można podnieść (wierzch magazynu, dobrana karta, wierzch śmietnika, wierzch dowolnej kolumny)
i która pasuje na fundament, jest tam zagrywana, w tej kolejności. Komputer nigdy nie „zapomina”.

| # | Zasada | Jak działa w kodzie |
|---|--------|---------------------|
| 1 | **Ścisły przymus** – as i kolejne karty na fundament, zawsze najpierw z magazynu, potem dobrana karta, śmietnik i kolumny | sprawdzany przed oceną ruchów; wierzch magazynu → fundament: +1000 |
| 2 | **Opróżniaj magazyn** – to warunek wygranej. Ale nie wykonuj ruchu, który przeciwnik od razu cofa: gdy po zdjęciu karty odsłonięta (już widoczna) karta może zostać znów zakryta przez kartę przeciwnika, a ja nie mogę jej sam zagrać, ruch jest odrzucany | każdy ruch z wierzchu magazynu: +500 (śmietnik: +90), odkrycie zakrytej karty: +80; ruch „do cofnięcia” (np. dama karo na wolną kolumnę, po czym przeciwnik odkłada ją na mojego króla karo): odrzucony. Z dwoma wolnymi kolumnami ruch jest dozwolony, bo król może pójść za damą |
| 3 | **Karm fundamenty** kartami z każdego źródła | ruch na fundament: +300 |
| 4 | **Kop w kolumnach** – przełóż kartę, jeśli odsłoni kartę na fundament albo taką, na którą pasuje wierzch magazynu | odsłonięta karta na fundament: +250, pasuje magazyn: +400 |
| 5 | **Blokuj przeciwnika** – kładź kartę na jego magazyn, gdy nie może jej od razu zagrać | +350, dodatkowo +200 gdy zakrywasz kartę pasującą na jego fundament; −40 gdy przeciwnik łatwo ją zdejmie |
| 6 | **Nie przykrywaj** kolumny, której wierzch pasuje na fundament | −150 |
| 7 | **Śmietnik i dobrana karta** – karta z własnego śmietnika dostaje bonus, dobraną kartę najpierw próbuj zagrać, odrzuć dopiero na końcu | śmietnik na fundament lub kolumnę +90, odsłonięcie karty na fundament +250, dobrana na kolumnę +60, na śmietnik przeciwnika +20 (daje dodatkowy ruch) |
| 8 | **Puste kolumny** zajmuj kartą z magazynu, drugi wybór to król. Innych kart tam nie kładź. Opróżnienie kolumny (wolne miejsce) też się liczy | magazyn +100, król +20, inne −60, wolne miejsce +60 (+20 bez magazynu) |
| 9 | **Płaskie kolumny** – im dłuższa kolumna docelowa, tym gorzej (łatwiej potem kopać) | −1 za każdą kartę w kolumnie |
| 10 | **Bez pętli** – żadnych ruchów kolumna → kolumna bez celu, ta sama karta nie jest przekładana dwa razy w turze | ruch bez korzyści: −30, powtórzenie: odrzucone |

Do każdej oceny dodawany jest mały losowy szum, żeby komputer nie grał w kółko tych samych ruchów.

## Poziomy

Wszystkie poziomy stosują te same zasady 1-4 i 6-10. Różnią się tylko trzema parametrami:

| Poziom | Pomijanie sensownych ruchów | Szum przy wyborze ruchu | Zasada 5 (blokowanie magazynu) |
|--------|-----------------------------|-------------------------|--------------------------------|
| Łatwy | co drugi ruch | duży (±60 pkt) | wyłączona |
| Normalny | co czwarty ruch | duży (±60 pkt) | wyłączona |
| Trudny | nigdy | mały (±3 pkt) | włączona |

Obowiązkowe zagranie na fundament nigdy nie jest pomijane. Komputer nie widzi ukrytych kart ani nie liczy
ruchów w przód, a podpowiedź zawsze używa ustawień poziomu Trudny.

W symulacjach komputer kontra komputer (`tools/sim.cpp`, 600 partii): Trudny wygrywa z Normalnym w ok. 78% partii,
a z Łatwym w ok. 98%. Dodatkowy, jeszcze silniejszy poziom (silniejsze blokowanie i kopanie) nie dał mierzalnej
przewagi nad Trudnym, bo wynik gry w dużej mierze zależy od losowania kart.

## Czego komputer nie robi

Komputer przekłada karty w kolumnach pojedynczo i nie przenosi całych sekwensów wieloma ruchami (to umie tylko gracz).

## Zakończenie gry

Wygrywa ten, kto pierwszy pozbędzie się wszystkich swoich kart: z magazynu, talii, dobranej karty i śmietnika.
Opróżnienie samego magazynu nie wystarcza. Gdy przez 24 tury z rzędu nikt nie wykona żadnego ruchu, jest remis.
Po 400 turach wygrywa ten, kto ma mniej kart do zagrania. W symulacjach ok. 90% partii kończy się
prawdziwym pozbyciem się wszystkich kart (średnio ok. 56 tur), a ok. 9% remisem.

## Zasady dla gracza (ludzkiego)

* **Kara za zapomnienie:** kto spróbuje dobrać kartę, odrzucić ją lub spasować, mając jeszcze kartę pasującą na fundament,
  traci turę (nic się wtedy nie dobiera ani nie odrzuca). Podpowiedź (H) zawsze wskazuje taką kartę w pierwszej kolejności.
* **Kliknięcie karty** przenosi ją na najlepszy z WSZYSTKICH dozwolonych ruchów, według tej samej oceny co ruchy komputera (ocena tylko szereguje ruchy: nawet ruch, którego komputer by nie wykonał, zostanie wykonany, jeśli jest jedyny, a cofnięcie jest zawsze pod ręką). Dobraną kartę odrzuca na śmietnik wyłącznie wtedy, gdy nie ma dla niej żadnego innego ruchu.
  nie ma nic lepszego (ocena do 20 punktów), odrzuca na śmietnik.
