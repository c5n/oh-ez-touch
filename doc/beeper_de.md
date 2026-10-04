# Der Signalgeber

Das Panel hat einen Piezo an einem GPIO, angesteuert von einem LEDC-Timer – und die Frequenz ist eine Eigenschaft des Timers. Deshalb steht zu jedem Zeitpunkt genau ein Ton zur Verfügung, und dieser eine Ton ist das gesamte Budget für achtzehn thematische Klänge aus drei Themenfamilien.

Es gibt hier zwei Engines, und sie geben dieses Budget unterschiedlich aus. Keine davon ist kostenlos; die Wahl zwischen ihnen ist Geschmackssache und keine Frage der Korrektheit.

## Die beiden Engines

**Der Sequencer** (`main/control/beeper_seq.{h,c}`, Standard) spielt jeweils eine Note und packt alles, was sonst auf eine zweite Stimme gekommen wäre, in diese Note: eine vierstufige Hüllkurve, einen Sweep, der die Periode statt der Frequenz durchlaufen kann, ein Vibrato, ein Tremolo und eine Wiederholungsanzahl. Ein Akkord wird als Arpeggio wiedergegeben. Der Schritt erfolgt alle fünf Millisekunden, sodass eine erklingende Melodie zweihundert Aufweckvorgänge pro Sekunde verursacht.

**Der Mixer** (`main/control/beeper_mixer.{h,c}`) spielt echte Akkorde durch Zeitmultiplexing: Jede von bis zu drei Stimmen belegt den Kanal der Reihe nach zwei Millisekunden lang – schnell genug, dass das Ohr sie zu einem Klang verschmilzt. Der Preis dafür ist ein rauer Klang bei jedem Akkord – das Zerhacken eines Tons mit 250 Hz erzeugt Seitenbänder auf beiden Seiten –, eine Untergrenze von etwa einem Kilohertz für alles, was übereinandergeschichtet wird, bis zu fünfhundert Neukonfigurationen des Timers pro Sekunde und eine auf drei feste Hüllkurvenformen beschränkte Ausdrucksmöglichkeit pro Note.

Die ehrliche Kurzfassung lautet: Der Sequencer ist besser bei *Noten*, der Mixer besser bei *Intervallen*. Was zählt, hängt von der Familie ab:

| Familie | mit dem Sequencer | mit dem Mixer |
| --- | --- | --- |
| Material | kaum anders – zehn der achtzehn Klänge waren ohnehin einstimmig | |
| Reticle | besser: Die Anschwellungen bekommen endlich das Vibrato, nach dem sie verlangt haben | |
| LCARS | | besser bei den übereinandergeschichteten Klängen: Die Umschalter, Bestätigungen und der anhaltende Alarm sind Intervalle, und ein Arpeggio ist kein Akkord |
| Classic | identisch, und genau das ist beabsichtigt: Es sind sechs Rechteckwellen mit fester Tonhöhe, übernommen aus der UI, die dieses Panel vor den beiden Engines hatte; daher verwendet es eine Hüllkurve, keinen Sweep und keinen Effekt | identisch |

LCARS ist der Grund, warum der Mixer noch existiert und nicht gelöscht wurde – allerdings weniger als früher, weil bei der unter *Die drei Familien* beschriebenen Überarbeitung die meisten Sweeps dieser Familie entfernt und durch gestufte Tonfolgen ersetzt wurden. Eine gestufte Tonfolge kann eine einzelne Stimme wiedergeben.

## Eine Engine auswählen

`idf.py menuconfig` → **OhEzTouch** → *Beeper engine*:

- `CONFIG_OHEZ_BEEPER_ENGINE_SEQ` (*Ausdrucksstark – Melodien, Hüllkurven, Sweeps, Vibrato und Tremolo*, Standard)
- `CONFIG_OHEZ_BEEPER_ENGINE_MIXER` (*Polyphon – bis zu drei ineinander verschachtelte Stimmen auf dem einen Piezo*)

Die Auswahl erfolgt beim Build, weil jede Engine ihr eigenes Notenformat und daher eigene Tabellen hat: Die vierundfünfzig Klänge sind doppelt geschrieben, einmal für jede Engine, und ein Panel enthält nur den Satz, den es abspielt. Ein Umschalten zur Laufzeit würde beide Sätze im Flash speichern – für eine Wahl, die niemand zweimal ändert.

Der Lanbon L8 hat keinen Summer, daher bewirkt diese Einstellung bei diesem Board nichts.

## Aufbau einer Melodie

```c
struct beeper_seq_note_s        /* twelve bytes, every one named */
{
    uint16_t f_start, f_end;    /* f_end == f_start is a steady note */
    uint16_t duration_ms, pause_ms;
    uint8_t  volume;            /* 0..100, the peak of the envelope */
    uint8_t  env, fx;           /* preset indices, see below        */
    uint8_t  repeat;            /* 0 AND 1 both mean once           */
};

struct beeper_seq_s { const struct beeper_seq_note_s *notes; uint8_t count; };
```

Die Melodie belegt acht Byte – genau so viel wie zuvor ein einzelner Signalton. Daher enthält die vier Einträge tiefe Anforderungswarteschlange weiterhin eine Melodie als Wert. Eine Melodie wird entweder vollständig abgespielt oder gar nicht, statt dass ihre letzten Noten einzeln verworfen werden.

Dass `repeat` zur Note und nicht zur Melodie gehört, ermöglicht es, einen Triller und einen Alarmrhythmus jeweils in einer Zeile zu beschreiben. Dass 0 und 1 gleich behandelt werden, ist Absicht: Eine Note, die in einer Tabelle wegen eines vergessenen Felds nur halb beschrieben wurde, soll *hörbar* sein, denn Stille ist der Fehlerzustand, den niemand bemerkt.

## Hüllkurven

Vier Stufen – Attack, Decay, Sustain, Release – und die Dauer der Note ist das Budget, kein Ereignis, das eintrifft. Bei einem Piezo gibt es kein Note-off: Die Länge eines Klangs ist bekannt, bevor er beginnt, daher ist die Release-Phase das Ende der Note und keine Reaktion auf irgendetwas.

Die drei Stufenlängen sind **Prozentsätze der Note mit einer Begrenzung in Millisekunden**; genau diese Kombination sorgt dafür, dass eine Zeile die ganze Schnittstelle beschreiben kann. Ein zwölf Millisekunden langer Kontakttick und eine vierhundert Millisekunden lange Startnote müssen wie dasselbe Instrument klingen. Ein festes Attack von fünf Millisekunden ist ein Sechstel der ersten Note und ein Achtzigstel der zweiten. Die Begrenzung löst das andere Ende desselben Problems: Sechzig Prozent von vierhundert Millisekunden ergeben ein zweihundertvierzig Millisekunden langes Fade-in – das ist kein Anschwellen.

`FLAT`, `CLICK`, `PLUCK`, `PAD`, `STAB`, `BELL`, `SWELL`, `BLOOM`. Die drei Formen des Mixers sind drei dieser Zeilen; es ging nichts verloren.

## Effekte

`sweep` verläuft über die Note von `f_start` → `f_end`, entweder linear in Hertz oder als **Glissando**, das die *Periode* interpoliert. Ein linearer Sweep in Hertz verbringt die meiste Zeit im oberen Bereich – von 1200 bis 2800 wird die erste Oktave in einem Drittel der Note durchlaufen –, weshalb die alten Chirps kopflastig klangen. Das Glissando ist bis auf ein Prozent geometrisch, benötigt eine Division und entspricht außerdem dem Verhalten der Hardware: Die Frequenz von LEDC ist ein Teiler, daher entspricht ein linearer Durchlauf des Teilers genau diesem Verlauf.

**Vibrato** ist ein Tonhöhen-LFO, dessen Tiefe in Promille *des Trägersignals* angegeben wird, sodass sich eine Zeile bei 1,2 kHz und 3,9 kHz gleich verhält. Eine feste Auslenkung in Hertz wäre am unteren Ende des Bands ein Sirenenton und am oberen ein Schimmer, und jede Zeile bräuchte ein Gegenstück. Es ist auch das eine, was der Mixer ausdrücklich nicht kann: In seinem Header steht, dass zwei Stimmen im Abstand von wenigen Hertz keinen Schwebungston erzeugen, weil die Phase nicht über eine Slot-Grenze hinweg erhalten bleibt.

**Tremolo** ist ein Amplituden-LFO und kann ausschließlich *absenken*. Ein aufwärts gerichtetes Tremolo würde eine Note über den von ihrer Hüllkurve angeforderten Spitzenwert hinaus anheben, und die Masterlautstärke hat oberhalb von 100 keinen Spielraum, um das auszugleichen.

Beide LFOs sind Dreieckswellen und beide starten bei jeder Note neu – ein Triller besteht also aus drei Anschlägen, bei denen das Vibrato jeweils an derselben Stelle einsetzt, statt jedes Mal an einer anderen.

`NONE`, `GLIDE`, `SHIMMER`, `WOBBLE`, `SIREN`, `BREATHE`, `PULSE`, `CHIRP`.

### Die zwei Regeln für die Rate

Es gibt eine Ober- und eine Untergrenze; für beide gibt es Host-Tests statt langer Erläuterungen.

`BEEPER_SEQ_LFO_MAX_CHZ` ist die Obergrenze: Oberhalb von etwa 25 Hz aliasiert das Wobbeln gegenüber dem Fünf-Millisekunden-Schritt, und der Simulator – der mit Audioauflösung berechnet – würde einen gleichmäßigen Klang hören, während das Panel einen gestuften hört. Außerdem begrenzt sie die Multiplikation des LFO selbst.

Die Untergrenze lautet: **Eine Note mit LFO dauert mindestens eine Periode dieses LFOs.** Ein LFO, der langsamer ist als seine Note, ist kein Ornament, sondern eine Tonhöhenbeugung: Die Note endet mitten im ersten Anstieg und kehrt nie zurück. Das fiel auf, als die ausgelieferten Tabellen zum ersten Mal durchlaufen und vermessen wurden: Drei der sechs Effektzeilen waren betroffen. In der Tabelle ist das unsichtbar – die Note sieht nach Vibrato aus, und die Effektzeile ebenso.

## Die drei Familien

Die Tabellen liegen in `main/ui/ui_beep_tables_seq.cpp` und `main/ui/ui_beep_tables.cpp`, eine pro Engine, und für beide gilt dieselbe Entwurfsrichtlinie:

- Frequenzen liegen zwischen etwa 1,1 und 3,9 kHz, denn dort ist ein kleiner Piezo laut. Die **Fehler**klänge brechen absichtlich mit dieser Regel: Schwer zu überhören ist wichtiger als laut zu sein, und ein Signalhorn, das wie der Rest der Oberfläche klingt, ist kein Signalhorn. `may_go_low()` in den Tests dokumentiert diese Ausnahme; ein zweiter Test prüft, dass sie weiterhin verwendet wird.
- Sofortige Rückmeldungen enden nach 150 ms, alles andere nach 700 ms. Danach klingt ein Tastendruck nicht mehr wie eine Bestätigung, sondern wie ein Echo.
- Die Kontaktebene – `UI_SOUND_PRESS` – hat etwa ein Drittel der Lautstärke von allem anderen. Die zugehörige Ebenenrichtlinie steht in `main/ui/ui_beep.hpp` und sollte gelesen werden, bevor etwas davon geändert wird.

### LCARS und was es nachahmt

Die Familie ist nach einer Konsole benannt, und die Tabelle wurde so überarbeitet, dass sie wie diese klingt und nicht wie Science-Fiction im Allgemeinen. Daraus ergaben sich vier Eigenschaften, alle strukturell, denn die Struktur kann ein Piezo wiedergeben, die Klangfarbe hingegen nicht:

- **Gestuft, nicht überblendet.** Ein Blip auf dieser Konsole besteht aus zwei bis vier aneinandergereihten, diskreten Tönen von jeweils fünfzehn bis vierzig Millisekunden – nicht aus einem Glissando. Die Tabelle verwendete früher ihre beiden meistgenutzten Klänge für eine Note, die in neunzig Millisekunden fast das gesamte Band überstrich. Das klingt eher nach einem Scan als nach einem Panel, das auf einen Finger reagiert. Zwei Sweeps bleiben erhalten: Der Ruf steigt, weil ein Ruf ansteigt, und das Signalhorn heult, weil ein Signalhorn heult.
- **Genauso oft fallend wie steigend.** Der bekannteste Konsolenklang ist ein abfallender Zweiton. Die alte Tabelle stieg fast überall an, weil ein Anstieg Zustimmung signalisiert und das Klangvokabular überwiegend zustimmend ist. Der Abfall gehört nun zu den beiden Gesten, bei denen es nicht weitergeht: etwas ausschalten und das Panel aufwecken.
- **Kurz.** Alles außer den drei Alarmen und der Tür liegt unter 150 ms – deutlich unter dem, was die Richtlinie erlaubt. Die Richtlinie ist eine Obergrenze, und diese Familie bleibt absichtlich weit darunter.
- **Ein Register.** Zwischen etwa 1,5 und 3,5 kHz; zur Unterscheidung dienen Rhythmus und Kontur statt Tonhöhe. Die Alarme weichen nach unten davon ab, wie es sich für Alarme gehört.

Niemand sollte das Ergebnis für eine Aufnahme halten. Es ist eine Rechteckwelle aus einem Piezo, während die nachgeahmten Klänge gesampelt, geschichtet und hallig sind. Was sich übertragen lässt, sind Kontur, Rhythmus, Register und Intervall. Das ist ein Großteil dessen, was einen Klang erkennbar macht, aber nicht alles. In den Kommentaren der Tabelle steht, wofür jeder Klang diese Eigenschaften nutzt.

### Der Türklingelton

`UI_SOUND_DOOR_CHIME` ist der achtzehnte Klang und der einzige ohne Aufrufstelle. Keine Geste auf einem Touchscreen bedeutet „Jemand steht vor der Tür“, daher ist er nur über MQTT erreichbar – siehe *Einen Klang abspielen* in [mqtt_de.md](mqtt_de.md). Er gehört zum Klangvokabular und wurde nicht daneben angebaut, damit jede Familie selbst entscheidet, wie eine Tür klingt, wie bei allem anderen auch, und damit die Tabellentests ihn derselben Richtlinie unterziehen. Jede Familie interpretiert ihn anders: Material schlägt zwei Töne an und lässt sie ausklingen; LCARS macht dasselbe, ist aber die einzige Stelle dieser Familie, an der eine Glocke statt eines kurzen Akzents erlaubt ist; Reticle schwillt an und klingt aus.

Ein Test in jeder Suite stellt sicher, dass dieser Klang keine Kopie seines Nachbarn ist. Das brauchen die anderen siebzehn nicht: Sie sind alle über die Bedienung des Panels erreichbar und ein falscher Klang würde auffallen.

Im Mixer wird nichts unterhalb von `BEEPER_POLY_MIN_HZ` übereinandergeschichtet, denn ein zwei Millisekunden langer Slot umfasst dort weniger als zwei Zyklen und die Tonhöhe löst sich in der Slotrate auf. Der Sequencer hat keine Polyphonie und braucht daher keine solche Regel. An ihre Stelle tritt die Vibratoauslenkung, die eine innerhalb des Bands geschriebene Note daraus hinausschwingen lassen kann.

## Die Demonstrationsmelodie

`main/control/beeper_song.c` enthält dreißig Sekunden Musik, die das gesamte Klangvokabular des Sequencers der Reihe nach vorführt. Sie existiert, weil die beiden obigen Abschnitte Prosa über Klänge sind. Niemand kann den Unterschied zwischen `STAB` und `PLUCK` aus „eine Kante, ein Abfall, ein kurzes Halten“ gegenüber „sofort, dann weg“ heraushören, und keine Themenfamilie stellt zwei Hüllkurven nebeneinander. Vor dieser Melodie bedeutete der Vergleich zweier Zeilen daher: Tabelle ändern, neu bauen und etwas drücken.

Sieben Abschnitte; die mittleren fünf sind bewusst lehrreich statt musikalisch:

| | | |
| --- | --- | --- |
| I | 2.9 s | eine Melodie, damit der Rest im musikalischen Kontext statt als Testtöne zu hören ist |
| II | 5.4 s | eine Tonfolge, achtmal wiederholt, einmal pro Hüllkurvenzeile |
| III | 3.9 s | derselbe Anstieg zuerst linear und dann als `GLIDE`, direkt hintereinander |
| IV | 5.6 s | eine gehaltene Tonhöhe mit `SHIMMER`, `WOBBLE`, `SIREN`, `BREATHE`, `PULSE` |
| V | 3.5 s | `repeat` als Triller und eine Tonleiter über zwei Oktaven |
| VI | 2.1 s | eine Tonhöhe in neun Lautstärken – nur die `volume` der Note |
| VII | 6.5 s | das Thema noch einmal, mit Verzierungen |

Es sind achtzig Noten, also ein Warteschlangeneintrag: `count` ist ein `uint8_t`, und `beeper_play_seq()` übergibt die Melodie als Wert. Das ganze Stück wird daher entweder abgespielt oder nicht. Es ist von genau zwei Regeln ausgenommen, die für die thematischen Tabellen gelten: `BEEPER_SEQ_NOTES_MAX` und die Obergrenze von 700 ms, denn beide betreffen einen *Signalton*, der auf eine Geste antwortet und die nächste nicht aufhalten soll. Alles andere gilt weiterhin, und `test_ui_beep_tunes.cpp` prüft es: das Band einschließlich Vibratoauslenkung, die LFO-Untergrenze, die Schrittuntergrenze und dass alle sechzehn Voreinstellungen tatsächlich benannt sind.

**Nur für den Sequencer.** Der Mixer hat ein anderes Notenformat und andere interessante Dinge, die er vorführen könnte. Ein zweites Stück für eine Engine zu schreiben, die hinter einem menuconfig-Schalter liegt, wäre Arbeit, um die niemand gebeten hat. `beeper_demo_available()` gibt dort `false` zurück, und die Schaltfläche wird nicht gezeichnet. Deshalb gibt es dafür eine Funktion, statt die UI mit einem `#if` zu belasten.

### Abspielen

Einstellungen → **Audio** → **Demo**. Während der Wiedergabe wird aus der Schaltfläche **Stop**; nach Ende der Melodie wechselt sie von selbst zurück. Verwendet wird die gerade *bearbeitete* Lautstärke statt der gespeicherten – genau wie bei **Test** und aus demselben Grund: Eine Lautstärke, die man erst hört, nachdem man sie gespeichert hat, ist keine Einstellung.

Drei wissenswerte Besonderheiten:

- **Während der Wiedergabe bleibt die UI stumm.** Nicht aus Höflichkeit: Die Warteschlange ist seriell, daher würde ein während der dreißig Sekunden ausgelöster Tastentick die Melodie nicht unterbrechen, sondern gespeichert und am Ende abgespielt. `ui_beep_play()` verwirft solche Ticks stattdessen.
- **Beim Verlassen der Audioseite wird die Wiedergabe gestoppt**, denn nur dort gibt es die entsprechende Steuerung. Eine Themenänderung ist die Ausnahme: Sie baut dieselbe Seite neu auf, und der automatische Nachtzeitplan kann jederzeit eine solche Änderung anfordern.
- **Das Stoppen erfolgt sofort**, wofür eine neue Funktion nötig war. `beeper_set_enabled(false)` konnte schon immer einen Klang abschneiden, ist aber eine *Einstellung*; `beeper_stop()` bewirkt dasselbe, ohne eine Einstellung zu ändern. Auf dem Panel wird die Wiedergabe beim nächsten Frame unterbrochen. Auf dem Simulator wurde die ganze Melodie an einen Audio-Callback übergeben; daher ist `port_beeper_stop()` der einzige Weg, sie zu beenden. Außerdem wartet `beeper_control` in 20-ms-Schritten auf eine gerenderte Melodie, statt in einem einzigen `vTaskDelay()` zu schlafen. Andernfalls bliebe die Aufgabe nach dem Abbruch bis zum Ende der nominellen halben Minute schlafend, während alle Tastenticks hinter ihr warteten.

## Anhören

Siehe [Das Panel hören](simulator_de.md#das-panel-hören). Der Simulator erzeugt die Impulsfolge, die der LEDC-Kanal ausgeben würde, und durchläuft dabei dieselbe Frame-Funktion wie das Panel. Es gibt keine zweite Syntheseimplementierung, die auseinanderlaufen könnte.

Er berechnet die Parameter der Engine auf deren eigenem Fünf-Millisekunden-Raster neu und führt nur den Oszillator mit höherer Auflösung. Würden die Parameter bei jeder einzelnen Abtastung neu berechnet, hätte der Simulator ein gleichmäßigeres Vibrato als die Hardware. Das ist die eine Richtung, in der ein Simulator nicht falsch liegen darf: Er würde das Panel besser klingen lassen, und eine darauf abgestimmte Tabelle klänge auf echtem Glas dann abgehackt.

Der Oszillator wird achtmal pro Ausgabesample abgetastet, und die Werte werden gemittelt. Ein Vergleich pro Sample – wie zuvor – aliasiert stark: Eine Impulswelle hat unendlich viele Harmonische, alles oberhalb der halben Abtastrate wird auf eine vom Ton unabhängige Frequenz zurückgefaltet, und bei einem gehaltenen Ton lag der hörbare Anteil inharmonischer Inhalte früher bei 4 bis 10 Prozent, jetzt bei 0,2 bis 0,6 Prozent. Aufgefallen ist das durch die Demonstrationsmelodie: Sie hält Noten lange genug, damit ein zurückgefalteter Teilton als Tonhöhe wahrnehmbar ist, und führt Sweeps so langsam aus, dass man hören kann, wie einer in die falsche Richtung läuft.

Dieses Artefakt gehört zum Renderer und nicht zum Panel: LEDC steuert den Pin mit einer echten Rechteckwelle, es wird nichts abgetastet und daher auch nichts zurückgefaltet. Damit war es derselbe Fehler wie im vorigen Absatz, mit derselben Auswirkung – deshalb wurde er in `port_beeper.c` behoben und nicht durch Abschwächen einer Tabelle.

## Tests

`test/host/` baut beide Engines und beide Tabellensätze in dieselbe Binärdatei:

- `test_beeper_mixer.cpp` / `test_beeper_seq.cpp` – die Arithmetik: Sweeps, die überlaufen würden, Hüllkurven, die unterlaufen würden, LFOs, die überlaufen würden, nicht terminierende Frame-Durchläufe und in beiden Dateien die Zusicherung, dass eine Vordergrundnote bei der ausgelieferten Masterlautstärke 25 weiterhin auf 63 Tastverhältniszähler kommt.
- `test_ui_beep_chimes.cpp` / `test_ui_beep_tunes.cpp` – die Tabellen, geprüft gegen die gemeinsame Richtlinie in `test_ui_beep_policy.hpp`. Die Signalton-Suite deckt außerdem `ui_sound_from_name()` ab, das eine `sound/set`-Nutzlast wieder in einen Klang umwandelt. Für diese Suche gibt es keine andere Schnittstelle, und ein nicht mehr passender Name würde nur als Logzeile auf einem unbeobachteten Panel auftauchen. Die Melodie-Suite enthält am Ende die Demonstrationsmelodie einschließlich der einen Zusicherung, die sich auf ihren *Zweck* statt auf ihre Arithmetik bezieht: Sie muss weiterhin jede Hüllkurve und jeden Effekt benennen. Wird eine neunte Zeile ergänzt und nicht vorgeführt, schlägt der Test fehl, statt dass sie unbemerkt bleibt.

Beide Sätze werden geprüft, obwohl ein Panel nur einen davon ausliefert. Der nicht ausgewählte Satz ist genau der, dessen Veralten niemand bemerken würde. Deshalb exportieren die beiden Tabellendateien unterschiedliche Typ- und Symbolnamen: Sie müssen nebeneinander gelinkt werden können.
