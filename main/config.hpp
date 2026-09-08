#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "ui_theme.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* The largest config file that will be read. Sized well above what the file
 * actually needs -- the shipped data/config.json is around 900 bytes of
 * pretty-printed JSON, and saveConfig() writes it back compacted to about half
 * that -- so that adding a setting does not silently push the file over the
 * limit and fall back to every default. It is still bounded, because the size
 * comes from the store and a corrupt SPIFFS can report anything; the buffer is
 * allocated from the real file size, not from this. */
#ifndef CONFIG_FILE_MAX_SIZE
#define CONFIG_FILE_MAX_SIZE 2048
#endif

/* The settings, and the file they live in.
 *
 * Both targets run the same code here: the file is reached through
 * port_storage, which is SPIFFS on the device and the host's config directory
 * in the simulator. The simulator used to have a second copy of loadConfig()
 * that assigned literals instead, which duplicated the defaults from
 * data/config.json and had already drifted from them; the `| default` fallbacks
 * in loadConfig() are now the only place a default is written down.
 */
class Config
{
public:
    struct
    {
        struct
        {
            char hostname[32];
        } general;
        struct
        {
            char hostname[32];
            int gmt_offset;
            bool daylightsaving;
        } ntp;
        struct
        {
            /* Stored by name in the file, as an enum here: an unknown name
             * cannot survive loadConfig(), so every consumer -- the styles, the
             * web form's dropdown -- gets a value that is valid by
             * construction. See ui_theme.hpp. */
            enum ui_theme_family_e theme;
            enum ui_night_mode_e night_mode;
            unsigned int night_from;
            unsigned int night_to;
        } ui;
        struct
        {
            unsigned long activity_timeout;
            unsigned int normal_brightness;
            unsigned int dim_brightness;
        } backlight;
        struct
        {
            bool enabled;
        } beeper;
        struct
        {
            char hostname[32];
            int port;
            char sitemap[32];
            struct
            {
                struct
                {
                    bool use;
                    int interval;
                    struct
                    {
                        char temperature[32];
                        char humidity[32];
                        char pressure[32];
                    } items;
                } bme280;
            } sensors;
        } openhab;
    } item;

    /** Mount the config store. Must succeed before loadConfig() is worth
     * calling; a false return means the settings will be the built-in defaults
     * and saveConfig() will fail. */
    bool setup();

    /**
     * Read `name` from the store into item.
     *
     * `name` is a bare file name -- "config.json", no leading slash: the store
     * owns the directory. Every field is assigned either way, so a missing,
     * oversized or unparseable file leaves a complete set of defaults rather
     * than whatever was in memory. The return value says whether the file was
     * actually read, which is what the caller needs to decide whether to offer
     * a first-run wizard, not whether item is usable.
     */
    bool loadConfig(const char *name);

    /** Write item back to the file loadConfig() was given. */
    bool saveConfig();

    /**
     * Hold this across a group of writes to item.
     *
     * There are two writers -- the web form's POST handler and the settings
     * screen's Save -- and since the web server got a task of its own they can
     * genuinely run at the same time. The handler writes item field by field
     * over twenty-odd fields, so without this a save from the panel during a
     * save from the browser could interleave them.
     *
     * Recursive, because settings_apply_live() is called with it held and
     * reads config through the same object.
     *
     * Readers are deliberately not locked. openhab_ui reads these fields on
     * every loop and taking a mutex there would cost more than the failure it
     * prevents: the worst a reader can see is one char[32] caught mid-strlcpy,
     * which shows as a truncated hostname for one iteration and is corrected on
     * the next. The writers are locked because a torn *write* is what would
     * persist.
     */
    void lock();
    void unlock();

private:
    SemaphoreHandle_t mutex = NULL;
    char config_filename[32] = "";
};

#endif
