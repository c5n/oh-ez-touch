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
    "word.physics": "Physics",
    "word.config": "Config",
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

    "tip.brokerSelect": "the broker to watch",
    "tip.brokerView": "show or hide the broker node",
    "tip.beaconsView": "show or hide the BLE beacons",
    "tip.physics": "how the boxes move",
    "tip.config": "MQTT configuration",

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
    "detail.controls": "Controls",
    "detail.topics": "Topics",
    "detail.remove": "remove from list",
    "detail.relay": "relay {n}",
    "detail.devStatus": " · {target} · v{version} · first seen {first}"
                        + " · last message {age} ago",
    "detail.beaconStatus": " · {what} · first seen {first}"
                           + " · last heard {age} ago",

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
    "physics.beacons": "Beacons",
    "hint.physics": "How the boxes move and the lines hang. Every slider"
                    + " acts on the canvas the moment it is touched; save"
                    + " keeps the feel, closing without saving puts it"
                    + " back. The panels, the beacons and the broker each"
                    + " have their own physics.",

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
    "word.physics": "Physik",
    "word.config": "Einstellungen",
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

    "tip.brokerSelect": "der beobachtete Broker",
    "tip.brokerView": "den Broker-Knoten zeigen oder verbergen",
    "tip.beaconsView": "die BLE-Beacons zeigen oder verbergen",
    "tip.physics": "wie sich die Boxen bewegen",
    "tip.config": "MQTT-Konfiguration",

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
    "detail.controls": "Steuerung",
    "detail.topics": "Topics",
    "detail.remove": "aus der Liste entfernen",
    "detail.relay": "Relais {n}",
    "detail.devStatus": " · {target} · v{version} · zuerst gesehen {first}"
                        + " · letzte Nachricht vor {age}",
    "detail.beaconStatus": " · {what} · zuerst gesehen {first}"
                          + " · zuletzt gehört vor {age}",

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
    "physics.beacons": "Beacons",
    "hint.physics": "Wie die Boxen sich bewegen und die Linien hängen."
                    + " Jeder Regler greift in dem Moment, in dem er"
                    + " berührt wird; speichern behält das Gefühl,"
                    + " Schließen ohne Speichern stellt es zurück."
                    + " Panels, Beacons und der Broker haben jeweils"
                    + " ihre eigene Physik.",

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
  document.dispatchEvent(new CustomEvent("langchange"));
}

function setLang(next) {
  if (!LANGS.includes(next) || next === lang) return;
  lang = next;
  try { localStorage.setItem("mqttviz-lang", lang); } catch (error) {}
  applyLang();
}

applyLang();
