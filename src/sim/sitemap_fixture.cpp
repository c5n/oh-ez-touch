/**
 * @file sitemap_fixture.cpp
 *
 * See sitemap_fixture.hpp.
 *
 * The JSON below is shaped like the responses of openHAB's
 * /rest/sitemaps/<sitemap>/<page>?type=json endpoint. Only the fields the
 * connector actually reads are filled in.
 *
 * Keep at most ITEM_COUNT_MAX (6) widgets per page, counting the implicit
 * "back" item that a page with a "parent" produces.
 */

#if (SIMULATOR == 1)

#include "sitemap_fixture.hpp"

#include <string.h>

#define FIXTURE_BASE "http://localhost:8080"

/*
 * Home: two sub pages plus a few read-only and switchable items.
 */
static const char page_demo[] = R"json(
{
  "id": "demo",
  "title": "OhEzTouch Demo",
  "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/demo",
  "leaf": false,
  "widgets": [
    {
      "widgetId": "00",
      "type": "Group",
      "label": "Living Room",
      "icon": "sofa",
      "linkedPage": {
        "id": "living",
        "title": "Living Room",
        "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/living",
        "leaf": false
      }
    },
    {
      "widgetId": "01",
      "type": "Group",
      "label": "Bedroom",
      "icon": "bedroom",
      "linkedPage": {
        "id": "bedroom",
        "title": "Bedroom",
        "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/bedroom",
        "leaf": false
      }
    },
    {
      "widgetId": "02",
      "type": "Text",
      "label": "Outside Temperature [3.5 °C]",
      "icon": "temperature",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Weather_Temperature",
        "type": "Number:Temperature",
        "name": "Weather_Temperature",
        "state": "3.5 °C",
        "stateDescription": {
          "pattern": "%.1f °C",
          "readOnly": true
        }
      }
    },
    {
      "widgetId": "03",
      "type": "Text",
      "label": "Doorbell [idle]",
      "icon": "text",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Doorbell_Status",
        "type": "String",
        "name": "Doorbell_Status",
        "state": "idle"
      }
    },
    {
      "widgetId": "04",
      "type": "Switch",
      "label": "All Lights",
      "icon": "light",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/gLights",
        "type": "Group",
        "groupType": "Switch",
        "name": "gLights",
        "state": "OFF"
      }
    },
    {
      "widgetId": "05",
      "type": "Slider",
      "label": "Hallway Dimmer [40 %]",
      "icon": "slider",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Light_Hallway",
        "type": "Dimmer",
        "name": "Light_Hallway",
        "state": "40"
      }
    }
  ]
}
)json";

/*
 * Living Room: the interactive widget types.
 */
static const char page_living[] = R"json(
{
  "id": "living",
  "title": "Living Room",
  "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/living",
  "leaf": true,
  "parent": {
    "id": "demo",
    "title": "OhEzTouch Demo",
    "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/demo",
    "leaf": false
  },
  "widgets": [
    {
      "widgetId": "10",
      "type": "Switch",
      "label": "Ceiling Light",
      "icon": "light",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Light_Ceiling",
        "type": "Switch",
        "name": "Light_Ceiling",
        "state": "ON"
      }
    },
    {
      "widgetId": "11",
      "type": "Slider",
      "label": "Floor Lamp [75 %]",
      "icon": "slider",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Light_Floor",
        "type": "Dimmer",
        "name": "Light_Floor",
        "state": "75"
      }
    },
    {
      "widgetId": "12",
      "type": "Colorpicker",
      "label": "RGB Strip",
      "icon": "colorpicker",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Light_Strip_Color",
        "type": "Color",
        "name": "Light_Strip_Color",
        "state": "200,80,60"
      }
    },
    {
      "widgetId": "13",
      "type": "Setpoint",
      "label": "Thermostat [21.5 °C]",
      "icon": "heating",
      "minValue": 10,
      "maxValue": 28,
      "step": 0.5,
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Thermostat_Setpoint",
        "type": "Number:Temperature",
        "name": "Thermostat_Setpoint",
        "state": "21.5 °C",
        "stateDescription": {
          "pattern": "%.1f °C"
        }
      }
    },
    {
      "widgetId": "14",
      "type": "Selection",
      "label": "Scene",
      "icon": "settings",
      "mappings": [
        { "command": "MOVIE", "label": "Movie" },
        { "command": "READING", "label": "Reading" },
        { "command": "PARTY", "label": "Party" },
        { "command": "OFF", "label": "Off" }
      ],
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Living_Scene",
        "type": "String",
        "name": "Living_Scene",
        "state": "READING"
      }
    }
  ]
}
)json";

/*
 * Bedroom: rollershutter and player, and a number with a percent pattern.
 */
static const char page_bedroom[] = R"json(
{
  "id": "bedroom",
  "title": "Bedroom",
  "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/bedroom",
  "leaf": true,
  "parent": {
    "id": "demo",
    "title": "OhEzTouch Demo",
    "link": ")json" FIXTURE_BASE R"json(/rest/sitemaps/demo/demo",
    "leaf": false
  },
  "widgets": [
    {
      "widgetId": "20",
      "type": "Switch",
      "label": "Blinds",
      "icon": "rollershutter",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Blinds_Bedroom",
        "type": "Rollershutter",
        "name": "Blinds_Bedroom",
        "state": "30"
      }
    },
    {
      "widgetId": "21",
      "type": "Switch",
      "label": "Radio",
      "icon": "receiver",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Radio_Control",
        "type": "Player",
        "name": "Radio_Control",
        "state": "PAUSE"
      }
    },
    {
      "widgetId": "22",
      "type": "Text",
      "label": "Humidity [48 %]",
      "icon": "humidity",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Bedroom_Humidity",
        "type": "Number:Dimensionless",
        "name": "Bedroom_Humidity",
        "state": "48",
        "stateDescription": {
          "pattern": "%d %%",
          "readOnly": true
        }
      }
    },
    {
      "widgetId": "23",
      "type": "Switch",
      "label": "Night Light",
      "icon": "light",
      "item": {
        "link": ")json" FIXTURE_BASE R"json(/rest/items/Light_Night",
        "type": "Switch",
        "name": "Light_Night",
        "state": "OFF"
      }
    }
  ]
}
)json";

static const struct
{
    const char *path;
    const char *body;
} fixture_pages[] = {
    { "/rest/sitemaps/demo/demo",    page_demo    },
    { "/rest/sitemaps/demo/living",  page_living  },
    { "/rest/sitemaps/demo/bedroom", page_bedroom },
};

const char *sim_sitemap_fixture_get(const char *url)
{
    if (url == NULL)
        return NULL;

    /* Skip the scheme and authority, so that the configured host and port do
     * not have to match the fixture. */
    const char *path = strstr(url, "/rest/");
    if (path == NULL)
        path = url;

    /* Compare without the query string. */
    size_t path_len = strcspn(path, "?");

    for (size_t i = 0; i < sizeof(fixture_pages) / sizeof(fixture_pages[0]); ++i)
    {
        if (   strlen(fixture_pages[i].path) == path_len
            && strncmp(fixture_pages[i].path, path, path_len) == 0)
        {
            return fixture_pages[i].body;
        }
    }

    return NULL;
}

#endif /* #if (SIMULATOR == 1) */
