/**
 * @file esp32/port_beeper.c
 *
 * The buzzer on LEDC.
 *
 * It owns LEDC_TIMER_1, which is the fix for the backlight bug described in
 * port_backlight.c: the frequency of a note is a property of the timer, not of
 * the channel, so a beeper that shares a timer with the backlight retunes the
 * backlight every time it sounds. That same fact is why this panel can only
 * ever make one tone at a time, and why a chord has to be interleaved --
 * see control/beeper_mixer.h.
 *
 * ------------------------------------------------------------------- volume
 *
 * BEEPER_DUTY_MAX is 512 of the 1024 counts LEDC_TIMER_10_BIT gives, and that
 * is the acoustic ceiling rather than a limit chosen for caution: the
 * fundamental of a square wave of duty D has amplitude (2/pi)*sin(pi*D), which
 * peaks at D = 0.5 and comes back down again. Duty 1024 is DC, and silent.
 *
 * It was 127 until there was a volume setting -- 12.4 %, a quarter of what the
 * pre-IDF code looked like it asked for. beeper_control.cpp configured 8-bit
 * resolution, and Arduino's ledcWriteTone() then reconfigured the timer to 10
 * bits before every note, so map(volume, 0, 100, 0, 127) landed out of 1024
 * rather than out of 256. Every beep this firmware ever made was at a quarter
 * of the intended duty, and nobody ever complained -- which is the only
 * evidence anyone has about how loud this panel should be.
 *
 * So that figure is preserved as the *default* rather than as the ceiling. The
 * chimes are written at note volume 50, the shipped master volume is 25, and
 * 50 * 255/100 * 25 * 10 / 255 = 124 per mille = 63 counts of duty: the same
 * 63 the old map() produced, to the count, and there is a host test that says
 * so. Turning the master up to 100 gives 254 counts and about eleven decibels
 * more sound than this panel has ever made.
 */
#include "port_beeper.h"

#include "board_pins.h"

#if OHEZ_HAS_BEEPER

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "port_beeper";

#define BEEPER_TIMER      LEDC_TIMER_1
#define BEEPER_CHANNEL    LEDC_CHANNEL_1
#define BEEPER_MODE       LEDC_LOW_SPEED_MODE
#define BEEPER_RESOLUTION LEDC_TIMER_10_BIT
/* Half of the 1024 counts the resolution above gives. See the file comment. */
#define BEEPER_DUTY_MAX   512
/* Only until the first note sets a real one. */
#define BEEPER_IDLE_FREQ_HZ 2000

static bool beeper_ready;

bool port_beeper_init(void)
{
    if (beeper_ready == true)
        return true;

    ledc_timer_config_t timer = {
        .speed_mode      = BEEPER_MODE,
        .duty_resolution = BEEPER_RESOLUTION,
        .timer_num       = BEEPER_TIMER,
        .freq_hz         = BEEPER_IDLE_FREQ_HZ,
        /* APB and not LEDC_AUTO_CLK, which matters now that a chord retunes
         * this timer every two milliseconds. AUTO prefers the timer-specific
         * REF_TICK whenever its divisor fits, which it does below about a
         * kilohertz and does not above -- so a chime that mixes registers
         * would flip the timer's clock source per slot, and each flip is a
         * full reconfiguration rather than a divider write. Pinning APB costs
         * nothing here: it puts the floor at about 76 Hz, and the lowest note
         * any table uses is 380. */
        .clk_cfg     = LEDC_USE_APB_CLK,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = OHEZ_BEEPER_PIN,
        .speed_mode = BEEPER_MODE,
        .channel    = BEEPER_CHANNEL,
        .timer_sel  = BEEPER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = { .output_invert = 0 },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    beeper_ready = true;

    ESP_LOGI(TAG, "beeper on GPIO %d", OHEZ_BEEPER_PIN);

    return true;
}

void port_beeper_tone(uint16_t freq, uint16_t level)
{
    if (beeper_ready == false)
        return;

    if (level > BEEPER_LEVEL_MAX)
        level = BEEPER_LEVEL_MAX;

    uint32_t duty = ((uint32_t)level * BEEPER_DUTY_MAX) / BEEPER_LEVEL_MAX;

    /* Deliberately not ESP_ERROR_CHECK, and that is a change: this is called
     * up to five hundred times a second while a chord sounds, and a frequency
     * LEDC cannot divide down to has to be a note that does not play, not a
     * panel that reboots. freq 0 is guarded because ledc_set_freq() divides by
     * it.
     *
     * Silence stays at whatever frequency was last set: retuning the timer to
     * stop a note would be pointless, and ledc_set_freq() on a timer at duty 0
     * still costs a reconfiguration. */
    if (duty != 0 && freq != 0 &&
        ledc_set_freq(BEEPER_MODE, BEEPER_TIMER, freq) != ESP_OK)
        duty = 0;

    (void)ledc_set_duty(BEEPER_MODE, BEEPER_CHANNEL, duty);
    (void)ledc_update_duty(BEEPER_MODE, BEEPER_CHANNEL);
}

/* The panel plays chimes the ordinary way, a step at a time, whichever engine
 * is arranging them. Only the simulator answers yes here. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
bool port_beeper_render_seq(const struct beeper_seq_s *seq, uint8_t master)
{
    (void)seq;
    (void)master;

    return false;
}
#else
bool port_beeper_render(const struct beeper_chime_s *chime, uint8_t master)
{
    (void)chime;
    (void)master;

    return false;
}
#endif

/* Nothing was handed over, so there is nothing to abandon: the walk lives in
 * beeper_control and breaks by itself. All that is left is to take the drive
 * off the pin, and doing it here rather than leaving it to the walk's own exit
 * is what makes a stop silent immediately instead of one frame later. */
void port_beeper_stop(void)
{
    port_beeper_tone(0, 0);
}

#else /* !OHEZ_HAS_BEEPER */

bool port_beeper_init(void)
{
    return false;
}

void port_beeper_tone(uint16_t freq, uint16_t level)
{
    (void)freq;
    (void)level;
}

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
bool port_beeper_render_seq(const struct beeper_seq_s *seq, uint8_t master)
{
    (void)seq;
    (void)master;

    return false;
}
#else
bool port_beeper_render(const struct beeper_chime_s *chime, uint8_t master)
{
    (void)chime;
    (void)master;

    return false;
}
#endif

void port_beeper_stop(void)
{
}

#endif /* OHEZ_HAS_BEEPER */
