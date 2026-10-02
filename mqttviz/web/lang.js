/* OhEzTouch MQTT Visualizer -- the two languages of the page.
 *
 * Every text the page shows lives here, keyed and looked up with t();
 * the static chrome is marked up with data-i18n in the HTML and filled
 * by applyLang(), the canvas and the built-in pieces call t() where
 * they draw. The choice is a setting like any other -- English or
 * German, kept by the server -- and the browser remembers it too, so
 * the page comes up in the right language before the first poll is
 * even back. tf() fills a {placeholder}; anything missing falls back
 * to English, and a key nobody has falls back to itself, so a missing
 * string can never blank the page. */
"use strict";

const LANGS = ["en", "de"];

const I18N = {
  en: {
    /* -- the words on the chrome ---------------------------------- */
    "word.broker": "Broker",
    "word.beacons": "Beacons",
    "word.fx": "FX",
    "word.physics": "Physics",
    "word.config": "Config",
    "word.layouts": "Layouts",
    "word.console": "Console",
    "word.close": "close",
    "word.save": "save",
    "word.defaults": "defaults",
    "word.play": "play",
    "word.copy": "copy",
    "word.clear": "clear",
    "word.online": "online",
    "word.offline": "offline",
    "word.heard": "heard",
    "word.asleep": "asleep",
    "word.on": "ON",
    "word.off": "OFF",

    "view.mesh": "Mesh",
    "view.topology": "Topology",
    "view.space": "3D",

    "tip.brokerSelect": "the broker to watch",
    "tip.brokerView": "show or hide the broker node",
    "tip.beaconsView": "show or hide the BLE beacons",
    "tip.fxView": "show or hide the light effects",
    "tip.modeMesh": "the classic picture: every panel on its line to the broker",
    "tip.modeTopology": "the network as it is: broker, access points, panels, beacons",
    "tip.modeSpace": "the network in the house: a SweetHome3D plan in 3D",

    /* -- the space view -------------------------------------------- */
    "space.import": "import SweetHome3D…",
    "space.replace": "replace house…",
    "space.remove": "remove house",
    "space.home": "look at the whole house",
    "space.opacity": "how solid the walls are drawn",
    "space.allLevels": "all",
    "space.unplace": "take out of the house",
    "space.emptyTitle": "No house yet",
    "space.emptyText": "Import a SweetHome3D file (.sh3d) and its walls, rooms, doors and windows become the stage. Saved with SweetHome3D 5.3 or newer.",
    "space.help": "drag a panel, access point or the broker over the plan: near a wall it sticks to it · shift+drag or shift+wheel: height · pick a level to reach the floors below · right drag: pan",
    "space.imported": "house {name}: {walls} walls, {rooms} rooms",
    "space.importFailed": "import failed: ",
    "tip.physics": "how the boxes move",
    "tip.config": "MQTT configuration",
    "tip.layouts": "saved arrangements of the canvas",
    "tip.zoomIn": "zoom in — or the wheel",
    "tip.zoomOut": "zoom out — or the wheel",
    "tip.zoomHome": "back to the middle: the whole canvas at 100%",

    /* -- the broker ----------------------------------------------- */
    "broker.notConnected": "not connected",
    "broker.chipOffline": "broker not connected",
    "broker.noBroker": "no broker",
    "broker.connectionOff": "connection off",
    "broker.inUse": "in use",
    "broker.waiting": "waiting for {topic}/#  —  nothing has published yet",

    /* -- the canvas ------------------------------------------------ */
    "chip.night": "NIGHT",
    "chip.backlight": "BACKLIGHT",
    "chip.active": "ACTIVE",
    "chip.brightness": "B ",

    "beacon.heardBy": "heard by ",
    "beacon.heardByPanels": "{n} panels",
    "beacon.nobody": "nobody",

    "age.agoSeconds": "{n}s ago",
    "age.seen": "seen {n} ago",

    /* -- the detail panel ------------------------------------------ */
    "detail.heardBy": "Heard by",
    "detail.panels": "Panels",
    "detail.controls": "Controls",
    "detail.topics": "Topics",
    "detail.remove": "remove from list",
    "detail.pin": "pin position",
    "detail.unpin": "release pin",
    "detail.relay": "relay {n}",
    "detail.devStatus": " · {target} · v{version} · first seen {first}"
                        + " · last message {age} ago",
    "detail.beaconStatus": " · {what} · first seen {first}"
                           + " · last heard {age} ago",
    "detail.apStatus": " · {bssid} · {n} panels · best signal {best}",

    /* -- the settings dialog ---------------------------------------- */
    "set.title": "MQTT brokers",
    "set.connected": "connected to the broker",
    "set.add": "add broker",
    "set.remove": "remove selected",
    "set.name": "name",
    "set.base": "base topic",
    "set.host": "broker host",
    "set.port": "broker port",
    "set.user": "username",
    "set.pass": "password",
    "set.language": "language",
    "set.verbose": "log every message",
    "hint.settings": "Saving keeps every profile; the one in use is picked"
                     + " with the selector in the top bar. The passwords"
                     + " are stored in mqttviz/data/mqttviz.json, which"
                     + " git does not keep.",
    "toast.settingsSaved": "saved — profiles kept, the connection follows"
                           + " the selector",

    /* -- the layouts dialog ------------------------------------------ */
    "lay.title": "Layouts",
    "lay.saveCurrent": "save current",
    "lay.load": "restore",
    "lay.delete": "delete",
    "lay.download": "backup file",
    "lay.import": "import backup",
    "lay.empty": "no saved layouts yet — arrange the canvas and give the"
                 + " arrangement a name",
    "lay.namePlaceholder": "name of the arrangement",
    "lay.nameNeeded": "the arrangement needs a name",
    "lay.meta": "{n} pinned · {mode} · saved {date}",
    "lay.meta3d": "{n} pinned · {n3d} in the house · {mode} · saved {date}",
    "hint.layouts": "Save the arrangement as it stands under a name and"
                    + " bring a saved one back whole — the places and the"
                    + " picture they belong to, which is what a layout is."
                    + " The server keeps a backup file beside the state"
                    + " (mqttviz/data/layouts.json, rewritten on every"
                    + " change and remembering what stood before the last"
                    + " restore), and the button here writes the same"
                    + " content to a file of your own. Importing adds, it"
                    + " never wipes.",
    "toast.layoutSaved": "layout saved: {name}",
    "toast.layoutLoaded": "layout restored: {name} — what stood before is"
                          + " in the backup file",
    "toast.layoutDeleted": "layout deleted: {name}",
    "toast.layoutImported": "{n} layouts imported",
    "toast.layoutImportFail": "not a layout file",
    "toast.backupWritten": "backup file written",

    /* -- the console ------------------------------------------------ */
    "lvl.debug": "debug",
    "lvl.info": "info",
    "lvl.warn": "warn",
    "lvl.error": "error",

    /* -- toasts ------------------------------------------------------ */
    "toast.unreachable": "mqttviz not reachable: ",
    "toast.switching": "switching to {name}",
    "toast.physSaved": "physics saved — this is how the boxes move now",
    "toast.lastBroker": "the last broker stays",
    "toast.copied": "console copied",
    "toast.copyFail": "could not copy",

    /* -- the physics panel -------------------------------------------- */
    "physics.nodes": "Panels",
    "physics.aps": "Access Points",
    "physics.beacons": "Beacons",
    "hint.physics": "How the boxes move and the lines hang. Every slider"
                    + " acts on the canvas the moment it is touched; save"
                    + " keeps the feel, closing without saving puts it"
                    + " back. The panels, the access points, the beacons"
                    + " and the broker each have their own physics.",

    "phys.drag": "viscosity",
    "phys.tether": "link spring",
    "phys.length": "link length",
    "phys.homing": "home pull",
    "phys.convection": "convection",
    "phys.convection_speed": "convection speed",
    "phys.shove": "shove firmness",
    "phys.wall": "wall spring",
    "phys.sag": "line sag",
    "phys.gravity": "gravity",
    "phys.signalPull": "signal pull",
    "phys.minSignal": "min signal",
    "phys.lineTimeout": "line timeout",
    "phys.metrePx": "px per metre",

    "help.drag": "How thick the oil is: how quickly a shoved box stops."
                 + " High = heavy, almost frozen; low = it keeps sliding.",
    "help.tether": "Every line drawn is a spring, and this says how hard"
                   + " it holds the length it wants. Weak = a loose mesh;"
                   + " strong = the network snaps crisply into place.",
    "help.length": "How long the links want to be: the radius of the ring"
                   + " the panels make around the broker, and how far the"
                   + " beacons sit from the panels that hear them.",
    "help.homing": "A gentle pull toward the middle for whatever the links"
                   + " do not hold -- a beacon nobody hears yet, a box"
                   + " thrown far. The links are what place the fleet;"
                   + " this is only the safety net.",
    "help.convection": "How far the boxes drift while idle -- the single"
                       + " slow swell that keeps the fleet alive.",
    "help.convection_speed": "How fast that swell rolls.",
    "help.shove": "How firmly two boxes that overlap push each other"
                  + " apart. The readout prints the effective value.",
    "help.wall": "How softly the edges of the canvas repel a box that"
                 + " reaches them.",
    "help.sag": "How far the lines droop below the straight path between"
                + " their two ends, like cables.",
    "help.signalPull": "How much a beacon line's hearing pulls: a strong"
                       + " signal shortens the leash, and this scales by how"
                       + " much. At ×0.00 every line wants the same length;"
                       + " at ×1.00 the radio alone says how far the beacon"
                       + " sits. Beacons only.",
    "help.minSignal": "The weakest hearing a line may carry: a panel whose"
                      + " signal falls below this is no longer connected --"
                      + " the line is gone, however fresh it heard. -100 dBm"
                      + " -- the shipped default -- lets every line stay."
                      + " Beacons only.",
    "help.lineTimeout": "How long a panel's last hearing keeps its line"
                         + " alive: a line whose panel has not reported the"
                         + " beacon for this long is gone, and a beacon with"
                         + " no lines left is gone with it. 30 to 300"
                         + " seconds. Beacons only.",
    "help.metrePx": "How many pixels a reported metre is worth, in the"
                    + " topology view: a beacon's distance estimate becomes"
                    + " a leash of metres scaled to this. Beacons that report"
                    + " no distance keep following their signal strength."
                    + " Beacons only.",
    "help.gravity": "Every object carries a gravity of its own and pulls"
                    + " on every other object: positive attracts, negative"
                    + " repels. Zero -- the shipped default -- leaves the"
                    + " mesh to its links. The readout prints the pull a"
                    + " neighbour at 100 px would exert.",
    "help.brokerGravity": "The pull of the hub itself: positive gathers"
                          + " the whole mesh toward the broker, negative"
                          + " blows it outward. The broker never moves;"
                          + " this is its one setting, and every line"
                          + " answers it.",

    "unit.perFrame": "/frame",
    "unit.ofLength": "% of length",
    "unit.grav100": "pull @ 100 px",
  },

  de: {
    /* -- die Worte der Oberfläche ---------------------------------- */
    "word.broker": "Broker",
    "word.beacons": "Beacons",
    "word.fx": "FX",
    "word.physics": "Physik",
    "word.config": "Einstellungen",
    "word.layouts": "Layouts",
    "word.console": "Konsole",
    "word.close": "schließen",
    "word.save": "speichern",
    "word.defaults": "Standard",
    "word.play": "abspielen",
    "word.copy": "kopieren",
    "word.clear": "leeren",
    "word.online": "online",
    "word.offline": "offline",
    "word.heard": "gehört",
    "word.asleep": "schläft",
    "word.on": "AN",
    "word.off": "AUS",

    "view.mesh": "Mesh",
    "view.topology": "Topologie",
    "view.space": "3D",

    "tip.brokerSelect": "der beobachtete Broker",
    "tip.brokerView": "den Broker-Knoten zeigen oder verbergen",
    "tip.beaconsView": "die BLE-Beacons zeigen oder verbergen",
    "tip.fxView": "die Lichteffekte ein- oder ausblenden",
    "tip.modeMesh": "das klassische Bild: jedes Panel an seiner Leine zum Broker",
    "tip.modeTopology": "das Netz, wie es ist: Broker, Access Points, Panels, Beacons",
    "tip.modeSpace": "das Netz im Haus: ein SweetHome3D-Plan in 3D",

    /* -- die Raumansicht ------------------------------------------- */
    "space.import": "SweetHome3D einlesen…",
    "space.replace": "Haus ersetzen…",
    "space.remove": "Haus entfernen",
    "space.home": "das ganze Haus zeigen",
    "space.opacity": "wie dicht die Wände gezeichnet werden",
    "space.allLevels": "alle",
    "space.unplace": "aus dem Haus nehmen",
    "space.emptyTitle": "Noch kein Haus",
    "space.emptyText": "Eine SweetHome3D-Datei (.sh3d) einlesen: ihre Wände, Räume, Türen und Fenster werden zur Bühne. Gespeichert mit SweetHome3D 5.3 oder neuer.",
    "space.help": "Panel, Access Point oder Broker über den Plan ziehen: nahe einer Wand haftet es an ihr · Shift+Ziehen oder Shift+Rad: Höhe · eine Ebene wählen, um die unteren zu erreichen · rechts ziehen: verschieben",
    "space.imported": "Haus {name}: {walls} Wände, {rooms} Räume",
    "space.importFailed": "Einlesen fehlgeschlagen: ",
    "tip.physics": "wie sich die Boxen bewegen",
    "tip.config": "MQTT-Konfiguration",
    "tip.layouts": "gespeicherte Anordnungen der Leinwand",
    "tip.zoomIn": "näher — oder das Rad",
    "tip.zoomOut": "ferner — oder das Rad",
    "tip.zoomHome": "zur Mitte: die ganze Leinwand bei 100 %",

    /* -- der Broker -------------------------------------------------- */
    "broker.notConnected": "nicht verbunden",
    "broker.chipOffline": "Broker nicht verbunden",
    "broker.noBroker": "kein Broker",
    "broker.connectionOff": "Verbindung aus",
    "broker.inUse": "in Gebrauch",
    "broker.waiting": "warte auf {topic}/#  —  noch nichts veröffentlicht",

    /* -- die Leinwand -------------------------------------------------- */
    "chip.night": "NACHT",
    "chip.backlight": "BELEUCHTUNG",
    "chip.active": "AKTIV",
    "chip.brightness": "H ",

    "beacon.heardBy": "gehört von ",
    "beacon.heardByPanels": "{n} Panels",
    "beacon.nobody": "niemand",

    "age.agoSeconds": "vor {n}s",
    "age.seen": "vor {n} gesehen",

    /* -- das Detail-Panel ---------------------------------------------- */
    "detail.heardBy": "Gehört von",
    "detail.panels": "Panels",
    "detail.controls": "Steuerung",
    "detail.topics": "Topics",
    "detail.remove": "aus der Liste entfernen",
    "detail.pin": "Position fixieren",
    "detail.unpin": "Fixierung lösen",
    "detail.relay": "Relais {n}",
    "detail.devStatus": " · {target} · v{version} · zuerst gesehen {first}"
                        + " · letzte Nachricht vor {age}",
    "detail.beaconStatus": " · {what} · zuerst gesehen {first}"
                           + " · zuletzt gehört vor {age}",
    "detail.apStatus": " · {bssid} · {n} Panels · bestes Signal {best}",

    /* -- der Einstellungsdialog ------------------------------------------ */
    "set.title": "MQTT-Broker",
    "set.connected": "mit dem Broker verbunden",
    "set.add": "Broker hinzufügen",
    "set.remove": "Auswahl entfernen",
    "set.name": "Name",
    "set.base": "Base-Topic",
    "set.host": "Broker-Host",
    "set.port": "Broker-Port",
    "set.user": "Benutzername",
    "set.pass": "Passwort",
    "set.language": "Sprache",
    "set.verbose": "jede Nachricht protokollieren",
    "hint.settings": "Speichern behält jedes Profil; welches benutzt wird,"
                     + " wählt die Auswahl in der Leiste oben. Die"
                     + " Passwörter liegen in mqttviz/data/mqttviz.json,"
                     + " das git nicht verwahrt.",
    "toast.settingsSaved": "gespeichert — die Profile bleiben, die"
                          + " Verbindung folgt der Auswahl",

    /* -- der Layout-Dialog -------------------------------------------- */
    "lay.title": "Layouts",
    "lay.saveCurrent": "aktuell speichern",
    "lay.load": "wiederherstellen",
    "lay.delete": "löschen",
    "lay.download": "Backup-Datei",
    "lay.import": "Backup einlesen",
    "lay.empty": "noch keine Layouts gespeichert — die Leinwand anordnen"
                 + " und der Anordnung einen Namen geben",
    "lay.namePlaceholder": "Name der Anordnung",
    "lay.nameNeeded": "die Anordnung braucht einen Namen",
    "lay.meta": "{n} fixiert · {mode} · gespeichert {date}",
    "lay.meta3d": "{n} fixiert · {n3d} im Haus · {mode} · gespeichert {date}",
    "hint.layouts": "Die Leinwand, wie sie gerade steht, unter einem Namen"
                    + " speichern und ein gespeichertes Layout als Ganzes"
                    + " zurückholen — die Plätze und das Bild, zu dem sie"
                    + " gehören, das ist ein Layout. Der Server hält eine"
                    + " Backup-Datei neben dem Zustand"
                    + " (mqttviz/data/layouts.json, bei jeder Änderung neu"
                    + " geschrieben, sie merkt sich auch, was vor dem"
                    + " letzten Wiederherstellen stand), und die"
                    + " Schaltfläche hier schreibt denselben Inhalt in eine"
                    + " eigene Datei. Einlesen ergänzt, es löscht nichts.",
    "toast.layoutSaved": "Layout gespeichert: {name}",
    "toast.layoutLoaded": "Layout wiederhergestellt: {name} — das"
                          + " vorherige steht in der Backup-Datei",
    "toast.layoutDeleted": "Layout gelöscht: {name}",
    "toast.layoutImported": "{n} Layouts eingelesen",
    "toast.layoutImportFail": "keine Layout-Datei",
    "toast.backupWritten": "Backup-Datei geschrieben",

    /* -- die Konsole -------------------------------------------------- */
    "lvl.debug": "Debug",
    "lvl.info": "Info",
    "lvl.warn": "Warnung",
    "lvl.error": "Fehler",

    /* -- Meldungen ------------------------------------------------------ */
    "toast.unreachable": "mqttviz nicht erreichbar: ",
    "toast.switching": "wechsle zu {name}",
    "toast.physSaved": "Physik gespeichert — so bewegen sich die Boxen jetzt",
    "toast.lastBroker": "der letzte Broker bleibt",
    "toast.copied": "Konsole kopiert",
    "toast.copyFail": "konnte nicht kopieren",

    /* -- das Physik-Panel ------------------------------------------------ */
    "physics.nodes": "Panels",
    "physics.aps": "WLAN APs",
    "physics.beacons": "Beacons",
    "hint.physics": "Wie die Boxen sich bewegen und die Linien hängen."
                    + " Jeder Regler greift in dem Moment, in dem er"
                    + " berührt wird; speichern behält das Gefühl,"
                    + " Schließen ohne Speichern stellt es zurück."
                    + " Panels, Access Points, Beacons und der Broker"
                    + " haben jeweils ihre eigene Physik.",

    "phys.drag": "Zähflüssigkeit",
    "phys.tether": "Leinen-Feder",
    "phys.length": "Leinenlänge",
    "phys.homing": "Heimzug",
    "phys.convection": "Konvektion",
    "phys.convection_speed": "Konvektionstempo",
    "phys.shove": "Schub-Festigkeit",
    "phys.wall": "Wandfeder",
    "phys.sag": "Liniendurchhang",
    "phys.gravity": "Gravitation",
    "phys.signalPull": "Signal-Zug",
    "phys.minSignal": "Mindestsignal",
    "phys.lineTimeout": "Leinen-Timeout",
    "phys.metrePx": "Px pro Meter",

    "help.drag": "Wie zäh das Öl ist: wie schnell eine geschubste Box"
                 + " stehen bleibt. Hoch = schwer, fast gefroren;"
                 + " niedrig = sie gleitet weiter.",
    "help.tether": "Jede gezeichnete Linie ist eine Feder, und dies sagt,"
                   + " wie fest sie ihre Länge hält. Schwach = ein loses"
                   + " Geflecht; stark = das Netz rast knackig ein.",
    "help.length": "Wie lang die Verbindungen sein wollen: der Radius des"
                   + " Rings, den die Panels um den Broker bilden, und wie"
                   + " weit die Beacons von den Panels sitzen, die sie"
                   + " hören.",
    "help.homing": "Ein sanfter Zug zur Mitte für alles, was die"
                   + " Verbindungen nicht halten — ein Beacon, das noch"
                   + " niemand hört, eine weit geworfene Box. Die"
                   + " Verbindungen ordnen die Flotte; dies ist nur das"
                   + " Fangnetz.",
    "help.convection": "Wie weit die Boxen im Leerlauf treiben — die"
                       + " eine langsame Dünung, die die Flotte leben"
                       + " hält.",
    "help.convection_speed": "Wie schnell diese Dünung rollt.",
    "help.shove": "Wie fest zwei überlappende Boxen einander"
                  + " auseinanderschieben. Die Anzeige zeigt den"
                  + " wirksamen Wert.",
    "help.wall": "Wie weich die Ränder der Leinwand eine Box"
                 + " zurückdrängen, die sie erreicht.",
    "help.sag": "Wie weit die Linien unter der geraden Verbindung"
                + " zwischen ihren beiden Enden durchhängen, wie Kabel.",
    "help.signalPull": "Wie stark das Hören an einer Beacon-Linie zieht: ein"
                       + " gutes Signal kürzt die Leine, und dies sagt, um"
                       + " wie viel. Bei ×0.00 will jede Linie dieselbe"
                       + " Länge; bei ×1.00 sagt allein das Radio, wie weit"
                       + " der Beacon sitzt. Nur Beacons.",
    "help.minSignal": "Das schwächste Hören, das eine Linie tragen darf: Ein"
                      + " Panel, dessen Signal darunter fällt, ist nicht"
                      + " mehr verbunden -- die Linie ist fort, so frisch sie"
                      + " auch hörte. -100 dBm -- der ausgelieferte Standard"
                      + " -- lässt jede Linie bleiben. Nur Beacons.",
    "help.lineTimeout": "Wie lange das letzte Hören eines Panels seine Linie"
                        + " am Leben hält: eine Linie, deren Panel den"
                        + " Beacon so lange nicht gemeldet hat, ist fort --"
                        + " und ein Beacon ohne Linien mit ihm. 30 bis 300"
                        + " Sekunden. Nur Beacons.",
    "help.gravity": "Jedes Objekt trägt eine Gravitation der eigenen und"
                    + " zieht an jedem anderen Objekt: positiv zieht,"
                    + " negativ schiebt. Null — der ausgelieferte Standard"
                    + " — überlässt das Netz seinen Verbindungen. Die"
                    + " Anzeige zeigt den Zug, den ein Nachbar in 100 px"
                    + " ausüben würde.",
    "help.brokerGravity": "Der Zug des Knotens selbst: positiv sammelt"
                          + " das ganze Netz um den Broker, negativ bläst"
                          + " es nach außen. Der Broker bewegt sich"
                          + " nie; dies ist seine einzige Einstellung,"
                          + " und jede Linie antwortet darauf.",
    "help.metrePx": "Wie viele Pixel ein gemeldetes Meter wert ist, in der"
                    + " Topologie-Ansicht: die Distanzschätzung eines Beacons"
                    + " wird zur Leine aus Metern, mit diesem Maßstab."
                    + " Beacons ohne Distanzmeldung folgen weiter ihrer"
                    + " Signalstärke. Nur Beacons.",

    "unit.perFrame": "/Bild",
    "unit.ofLength": "% der Länge",
    "unit.grav100": "Zug bei 100 px",
  },
};

/* The browser remembers the language between visits, so the page comes
 * up right immediately; the server's setting confirms or corrects it
 * on the first poll. */
let lang = "en";
try {
  const stored = localStorage.getItem("mqttviz-lang");
  if (LANGS.includes(stored)) lang = stored;
} catch (error) { /* no storage, no memory -- English until the poll */ }

function t(key) {
  return (I18N[lang] && I18N[lang][key]) || I18N.en[key] || key;
}

function tf(key, vars) {
  let out = t(key);
  for (const name in vars) {
    out = out.split("{" + name + "}").join(vars[name]);
  }
  return out;
}

function applyLang() {
  document.documentElement.lang = lang;
  for (const el of document.querySelectorAll("[data-i18n]")) {
    el.textContent = t(el.dataset.i18n);
  }
  for (const el of document.querySelectorAll("[data-i18n-title]")) {
    el.title = t(el.dataset.i18nTitle);
  }
  for (const el of document.querySelectorAll("[data-i18n-placeholder]")) {
    el.placeholder = t(el.dataset.i18nPlaceholder);
  }
  document.dispatchEvent(new CustomEvent("langchange"));
}

function setLang(next) {
  if (!LANGS.includes(next) || next === lang) return;
  lang = next;
  try { localStorage.setItem("mqttviz-lang", lang); } catch (error) {}
  applyLang();
}

applyLang();
