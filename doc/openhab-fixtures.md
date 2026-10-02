# Testing against a real openHAB

`main/sim/sitemap_fixture.cpp` is enough to draw a screen, and the host tests in
`test/host` are enough to pin what the parser does with it. Neither can tell you
what openHAB *actually sends*, and that turns out to be the interesting
question: the fixture and its tests were originally written from the REST
documentation, and four of the things they assumed were wrong.

So `test/openhab/` holds sitemaps and items to point a real server at. They are
not a demo of the panel; they are shaped to reach the corners -- a navigation
tree deeper than anything a screenshot shows, one item of every type openHAB
has, and pages built specifically to overflow the panel's fixed arrays.

Everything below was measured against **openHAB 5.2.1** on 2026-09-15, and the sitemap stream on 2026-10-02. Where it
says openHAB does something, that is what that server did, not what the
documentation claims.

## Installing them

openHAB reads `.items` and `.sitemap` files out of its configuration directory
and reloads within about ten seconds of a write. On a snap install:

```bash
OH=/var/snap/openhab/current/conf      # "current" is a symlink to the revision
cp test/openhab/items/*.items       $OH/items/
cp test/openhab/sitemaps/*.sitemap  $OH/sitemaps/
```

That directory is **root-owned out of the box**, and creating items over REST
answers 401, so on a fresh machine this needs either `sudo` or a one-time
`chmod` of the two directories. Watch `openhab.log` for `Loading DSL model
'<name>'` to know it took, and for the validation errors it prints when it did
not:

```bash
tail -f /var/snap/openhab/current/userdata/logs/openhab.log
```

`/rest/sitemaps` is the other confirmation -- a sitemap that failed to parse
simply is not in the list.

Then give the items states, because one that has never been told anything reads
`NULL` and a panel full of `NULL` shows nothing about how the panel draws:

```bash
test/openhab/seed-states.sh                    # or ... http://host:8080
```

## Pointing the panel at one

```bash
tools/ohez_ctl.py set oh_sitemap oheznav       # applies without a restart
tools/ohez_ctl.py wait-page
```

`set` writes a real `config.json`, so isolate a run with `OHEZ_CONFIG_DIR`
pointing somewhere disposable.

## What is here

| sitemap | file | what it is for |
| --- | --- | --- |
| `demo` | `demo.sitemap` | six widget *types* on one page. The quickest way to compare themes or check a control end to end. |
| `oheztest` | `oheztest.sitemap` | the shapes `demo` has no example of: a Group with an aggregate function, a dimensioned `Number:Temperature`, a `Number:Dimensionless`, a Dimmer, a String holding a space. |
| `oheznav` | `oheznav.sitemap` | a five-level navigation tree, one item of every openHAB type, and four pages that deliberately exceed a panel limit. |
| `ohezevents` | `ohezevents.sitemap` | everything about a widget that is not its state: colour rules, a visibility rule on a widget and on a Frame, a read-only item, state options, command options, a MAP and a static icon. Watched over `/rest/sitemaps/events`. |

`demo.*` came with the test server. The other three were written to answer
questions the fixture could not.

### `ohezevents` in particular

`EV_Show` drives both visibility rules. `EV_Temp` drives the three colour
rules: below 5 °C the value is light blue, above 25 °C it is orange and the
icon red, above 30 °C the label is red too. Set them over REST and watch the
tiles follow without a page load:

```bash
curl -X PUT -H 'Content-Type: text/plain' --data-binary '30 °C' \
     http://localhost:8080/rest/items/EV_Temp/state
curl -X PUT -H 'Content-Type: text/plain' --data-binary ON \
     http://localhost:8080/rest/items/EV_Show/state
```

The MAP on `EV_Window` uses the stock `de.map`. Without the MAP
transformation add-on installed, openHAB shows the raw state.

### `oheznav` in particular

```
OhEzTouch Navigation
├── Ground Floor → Living Room → Lighting → Scenes     ← four levels below home
│   ├── Kitchen
│   └── Lights On              (Group → openHAB builds the member page)
├── First Floor → Bedroom
│   ├── Average [20,2 °C]      (Group:Number:AVG)
│   └── Total [1335 W]         (Group:Number:SUM)
├── Sensors                    (Contact, DateTime, Location, String)
└── Edge Cases → Too Many Tiles / Framed
```

The corners it aims at, each of which found something:

- **Scenes** is four levels below home, which is deeper than the panel's own
  navigation was ever tested to.
- **Too Many Tiles** offers ten widgets against `ITEM_COUNT_MAX` of six.
- **Edge Cases** carries a twelve-entry Selection against
  `ITEM_SELECTION_COUNT_MAX` of ten, a forty-character label against
  `STR_LABEL_LEN` of thirty-two, and `Außentemperatur Süd` for the label
  trimmer, which used to hand bytes above 0x7F to `isspace()`.
- **Framed** is two `Frame` blocks. See below -- this one was not a corner case
  at all.
- `NAV_Unset` is never commanded, so it stays `NULL`.
- `NAV_Spaced` holds `Hello World`, which is the shape that breaks the icon URL.

## What a real openHAB sends

The shapes that were guessed wrong, and are now in the fixture and pinned by
`test/host/main/test_sitemap_parse.cpp`:

- **Every widget carries `mappings`**, an empty array when the sitemap declares
  none. An empty JSON array is *truthy* to ArduinoJson, so
  `if (widget["mappings"])` was true for every widget a real server sends, and
  the parser's fallback to the item's `commandDescription.commandOptions`
  never ran against openHAB 5. The parser tests the size now.
- **`Text label="..." { ... }` has no `item` key whatsoever** -- a Text widget
  with a `linkedPage` and nothing else. It is the commonest way to make a
  sub-page and the panel's `type_link`.
- **A `Group` widget carries both**: its group item, with `members`,
  `groupType` and `function`, *and* a `linkedPage` to a member page openHAB
  generates. The group's state is real and aggregated, so the tile is updated
  like any other. Its changes arrive on the event stream as
  `GroupItemStateChangedEvent`, on `items/<group>/<member>/statechanged`, which
  is why the panel subscribes to that topic as well for a group.
- **A Player arrives with mappings openHAB wrote itself** -- PREVIOUS, PAUSE,
  PLAY, NEXT -- from a bare `Default item=...` line. The item's type has to win
  over the presence of mappings or the tile becomes a selection.
- **A dimensioned item carries its unit three times**: on the state
  (`"21.5 °C"`), on the widget as `unit`, and on the item as `unitSymbol`. The
  connector reads none of them and gets temperatures right only because the
  display pattern repeats the unit.
- **The widget's `pattern` is derived from the item's `stateDescription`**, not
  the other way round -- confirmed with an `.items` label that carried the
  pattern and a `.sitemap` line that did not.
- **A plain `Number` reads `"10.000000"`**, six decimals and no unit, whatever
  the display pattern says.
- **An uninitialised item reads the four characters `"NULL"`** -- not a JSON
  null, not an empty string -- while the label openHAB builds for it says `-`.
- **Sub-page ids are widget ids** (`1_5`), and on a group's generated page the
  `widgetId` of each row is the *item name*.
- **A missing sitemap answers 404 with a body that is JSON-encoded twice**, so
  its top level is a string; a missing item's `/state` answers 404 with a bare
  error object. Both are refused on the status before the parser sees them.
- **Icon sets are SVG-only.** `/icon/<name>?state=..&format=png` is a 404 on a
  stock server; `format=svg` is a 200.
- A German-locale server writes the value into the label with a **decimal
  comma** (`Temperature [21,4 °C]`). All of it is stripped, comma included.
- **openHAB announces itself over mDNS**, as `_openhab-server._tcp.local` on
  the REST port and `_openhab-server-ssl._tcp.local` on 8443, with the TXT
  record `uri=/rest` on both. A unicast-response query -- the `QU` bit, which
  lets a client use an ordinary ephemeral socket instead of joining the group
  -- is answered in one packet carrying PTR, A, TXT and SRV, in 56 to 70 ms on
  a wired network. The SRV record's owner name is a compression pointer into
  the middle of the PTR record's data, which itself ends in a pointer to the
  question, so a parser that does not follow pointers finds nothing. That
  packet is in `test/host/main/test_mdns_query.cpp` byte for byte, captured on
  2026-09-18, and is what the settings screen's list of servers is built from.
- **There is no `transformedState` any more.** What openHAB formatted -- a
  `MAP(...)`, a date pattern, a decimal comma -- is only in the label's
  `[...]`. The parser takes that as the transformed state.
- **A widget a visibility rule hides is not on the page at all.** It is left
  out of `widgets`, not sent with `"visibility": false`. Its events still come
  on the sitemap stream, as `"visibility": false` while it is hidden.
- **The children of a hidden Frame report themselves visible.** The Frame's
  own event says it is hidden; the child's says `"visibility": true`,
  whatever the Frame is.
- **Command options mirror state options.** An item with only
  `stateDescription` options arrives with the same list as
  `commandDescription.commandOptions`.
- **The sitemap stream** (`/rest/sitemaps/events`):
  - `POST .../subscribe` answers 200 with the id at the end of
    `context.headers.Location`, with the host the request named.
  - A GET on an id openHAB does not know is never answered: no status line,
    no error.
  - A GET on a good id is not answered either until there is a first event.
    Even the status line waits, and on a quiet page that is the keepalive,
    50 s in one measurement.
  - The keepalive is `{"TYPE":"ALIVE",...}`, about once a minute. An edited
    sitemap file sends `{"TYPE":"SITEMAP_CHANGED",...}`.
  - A widget event is 500 to 900 bytes and carries:
    - `widgetId`
    - the formatted label
    - `labelcolor`, `valuecolor` and `iconcolor`, when a rule applies
    - `visibility`, `reloadIcon` and `descriptionChanged`
    - the whole item, with its raw state
    - `sitemapName` and `pageId`
  - Widget ids are `1_0`, `1_5`, `1_50`, … on ordinary pages. On a group's
    generated page they are the members' item names. The root page's id is
    the sitemap name.
- **`GET /rest/sitemaps` is a flat array**, one object per sitemap with `link`,
  `name`, `label` and a `homepage` object -- and that homepage's `widgets` is
  **empty** here, however many the page really has; the widgets only come with
  the page request itself. About 210 bytes per sitemap. `label` is optional: a
  `sitemap x label="..."` line supplies it and a sitemap without one has no
  `label` key at all. This is what the settings screen and the web form offer as
  the choice of sitemap, parsed under a filter that keeps `name` and `label` and
  drops the rest -- `test/host/main/test_sitemap_list.cpp`, against a capture
  from 2026-09-18.

- **An Image item's state is the picture**, as measured on 2026-10-02 against
  openHAB 5.2.1 with a UI-managed sitemap:
  - In a sitemap page, the widget carries the item with the whole
    `data:image/jpeg;base64,...` state, and the previous one again as
    `lastState`. A 14 KB JPEG made a one-widget page 23.8 KB. The panel cuts
    every JSON string over 512 bytes short while it reads
    (`main/openhab/json_squeeze.c`), and the same on the event streams, where
    a new snapshot is a widget event of the same size.
  - The widget's `url` is `/proxy?sitemap=...&widgetId=...` whether it has an
    item or not. For an item-backed one openHAB answers it with **HTTP 500**
    ("URL 'null' is not a valid URL"). The panel never uses it.
  - `GET /rest/items/<name>/state` answers `text/plain` with the data URI by
    default, and the **raw JPEG** with `Content-Type: image/jpeg` for
    `Accept: image/*` or `image/jpeg`. That is what the panel asks for.
  - An Image widget with `url=` and no item has no `item` at all. The panel
    leaves it out.
  - `test_sitemap_parse.cpp` pins the widget, `test_json_squeeze.cpp` the
    page.

## Where the panel and openHAB disagree

Known, reproduced against this server, and not fixed. Any of these is a real
bug rather than a fixture artefact.

1. **`Frame` rendered an empty page.** Fixed. openHAB nests the real widgets
   inside the Frame's own `widgets` array. The connector now descends one
   level: a Frame's children become tiles of the page, in order, and the Frame
   itself, having no tile of its own, is dropped. The one exception is the
   home page's clock frame (the `clock_frame` setting, `Clock` by default),
   whose first three children go to the clock screen instead. `oheznav`'s
   **Framed** page is still the reproduction, and
   `test/host/main/test_sitemap_parse.cpp` pins both cases.
2. **Icon URLs are not percent-encoded.** `Item::iconUrl()` pastes the state
   into the query string as-is, and `esp_http_client_set_url()` then refuses the
   URL outright -- it never reaches the server, and `session_prepare()` turns
   the refusal into a disconnect, so the cost is a torn-down keep-alive once per
   icon refresh. A space does it (`NAV_Spaced`) and so does any non-ASCII byte
   (`NAV_Umlaut` holding `Süd`). Numbers never reach it, because the parser
   re-prints them through `%f` first. Fixing it means encoding *and* widening
   `STR_URL_LEN`: a worst-case 32-byte state triples to 93 and pushes the widest
   legal URL to 276, past the 256 that `test_item_urls.cpp` guarantees fits.
3. **openHAB's formatted value is used only by text tiles.** A String,
   DateTime, Location or Contact tile shows the label's `[...]`, kept up to
   date by the sitemap stream. On the item stream it falls back to the raw
   state at the first change. A number tile still formats the number itself, so
   an AVG group shows `20.15` where openHAB says `20,2 °C`.
4. **`Number:Dimensionless` is wrong in both directions.** Commanded `"48 %"`,
   openHAB stores the ratio `"0.48"` and the tile reads `0 %`; commanded a bare
   `48` it stores `"48"` and the tile reads `48 %` while openHAB's own label
   says `4800 %`. Closing it needs the unit the panel drops.
5. **Both clamps are silent.** A ten-widget page draws five widgets and a back
   tile; a twelve-entry Selection lists ten. Nothing on screen says anything was
   dropped. Hidden widgets and widget types the panel cannot draw are skipped
   before the count, so they no longer use up a place.
6. **A server that goes away after the first load is not reported.** An
   edited sitemap reaches the panel over the sitemap stream now, but a server
   that goes away *after* the first load is
   invisible: the stream reconnects quietly, every poll fails silently,
   `page.state` stays `ready`, and no banner appears. `SITEMAP ACCESS FAILED` only fires on a
   *page* fetch.

## The panel's fixed limits

Worth having beside you when writing a page that is meant to fit.

| limit | value | where |
| --- | --- | --- |
| widgets per page | 6, **including** the back tile | `ITEM_COUNT_MAX` |
| entries in a Selection | 10 | `ITEM_SELECTION_COUNT_MAX` |
| label | 32 bytes | `STR_LABEL_LEN` |
| state text | 32 bytes | `STR_STATE_TEXT_LEN` |
| sitemap page body | 12288 bytes, after strings over 512 bytes are cut | `OPENHAB_CLIENT_PAGE_BUFFER_SIZE`, `JSON_SQUEEZE_STRING_MAX` |
| a picture, as decoded | half the screen each way, or less | `main/ui/items/item_image.cpp` |
| icon body | 5000 bytes | `OPENHAB_CLIENT_ICON_BUFFER_SIZE` |
| `/rest/sitemaps` body | 8192 bytes | `OPENHAB_CLIENT_SITEMAPS_BUFFER_SIZE` |
| sitemaps offered | 12 | `SITEMAP_LIST_COUNT_MAX` |
| servers an mDNS scan lists | 8 | `OPENHAB_DISCOVER_COUNT_MAX` |
| mDNS response read | 512 bytes | `MDNS_QUERY_PACKET_MAX` |

A real six-widget page is about 3 KB of JSON, so the page buffer is not the
constraint it looks like.

## Driving the panel through all this

`doc/test-interface.md` is the reference; two things bite specifically when
walking a deep tree:

- **Go up a page with `tap-tile 0`**, the parent-link tile. Not with a swipe:
  the panel has no swipe gestures at all, and before they were taken away a
  swipe only *appeared* to navigate, in one theme, by landing as a click on
  whatever sat where the swipe began.
- **Leave an item screen with `tap 20 20`**, its back bar.
- `wait-page` after every navigation. `page.state` is the fetch cycle, and a
  tap before it says `ready` does nothing at all.

## Removing the fixtures

```bash
OH=/var/snap/openhab/current/conf
rm $OH/items/{oheztouch_gaps,oheznav}.items $OH/sitemaps/{oheztest,oheznav}.sitemap
```

`demo.*` is the server's own; leave it.
