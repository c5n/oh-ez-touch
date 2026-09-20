/**
 * @file linux/port_beeper.c
 *
 * The simulator's buzzer: SDL audio, synthesising the pulse train the panel's
 * LEDC channel would be producing.
 *
 * A desktop has no piezo, and this used to say so and stop -- which left the
 * chime tables the one part of the interface nobody could check without
 * flashing an ArduiTouch. They are also the part with fifty-one hand-written
 * entries in it. SDL2 is already linked into this target for the display and
 * the mouse, and LVGL only ever initialises its video subsystem, so the audio
 * one was there for the asking.
 *
 * -------------------------------------------------------- why render, not follow
 *
 * This does not follow port_beeper_tone(). It takes the whole chime and walks
 * beeper_mixer_frame() itself, at the sample rate.
 *
 * The reason is the tick. CONFIG_FREERTOS_HZ is 250 here -- deliberately, see
 * sdkconfig.defaults.linux, where every tick is a SIGALRM and a high rate is a
 * high EINTR rate in SDL and X11 -- so a task on this target cannot honour the
 * two-millisecond slot the device interleaves voices on. Following the tone
 * calls would render every chord four times coarser than the panel plays it,
 * and the simulator would be wrong in the one direction that does damage: it
 * would sound worse than the hardware, and somebody would go and "fix" a table
 * that was fine.
 *
 * Walking the mixer instead is exact, and it costs nothing extra to keep in
 * step, because it is the same function the device walks -- which is what
 * beeper_mixer_frame() being pure is for.
 *
 * ------------------------------------------------------- the one artefact
 *
 * A pulse has harmonics without end, and this is a sampled medium, so every
 * harmonic above half the sample rate folds back down -- and it lands at a
 * frequency that has nothing to do with the note. A D7 at 2349 Hz put partials
 * at 1020 and 1328 Hz; B7 at 3951 put one at 588 Hz, a fifth of the amplitude
 * of the note itself. It got worse as the volume went *down*, because the
 * shipped master of 25 makes the duty 12.5 % and a narrow pulse is spectrally
 * far richer than a square. On a slow sweep the folded partials ran downward
 * while the note rose, which is the sound everybody recognises as aliasing.
 *
 * None of which the panel does. LEDC drives the pin with a real square wave:
 * nothing is sampled, so nothing folds, the harmonics stay harmonic, and they
 * sit above the piezo's resonance where it cannot radiate them anyway. So this
 * was the simulator inventing a defect the hardware has not got -- the one
 * direction it must not be wrong in, again, and the reason the fix is here
 * rather than in a table.
 *
 * OVERSAMPLE below is the whole of it: the pulse is sampled eight times per
 * output sample and averaged, so a sample straddling an edge comes out
 * part-way rather than all the way. What it explicitly does NOT do is
 * re-evaluate the engine: render_advance() still runs on output samples, so
 * the sweep, the envelope and both LFOs are still read on the panel's own
 * five-millisecond grid. That distinction is the whole of the section above,
 * and oversampling the oscillator does not touch it -- it removes an artefact
 * of this renderer, not a coarseness of the hardware.
 *
 * ------------------------------------------------------------ what it is not
 *
 * A caricature of the transducer, and it should be listened to as one. The
 * ArduiTouch's piezo has a sharp mechanical resonance somewhere around two to
 * four kilohertz, so on a panel a note at the peak can be ten or twenty
 * decibels above one an octave away, and the deliberately low error tones will
 * be far quieter in a room than they are here. Nothing below models that, or
 * the ringing after the drive stops, or the case it is glued into, or LEDC's
 * frequency quantisation, or the jitter of a slot loop running next to LVGL.
 *
 * This is cleaner than the hardware, unavoidably. Judge rhythm, contour,
 * intervals and balance here; judge loudness on a panel.
 */
#include "port_beeper.h"

#include <math.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "esp_log.h"

static const char *TAG = "port_beeper";

#define RATE        48000
#define BUFFER      512

/* Sub-samples of the pulse per output sample -- see "the one artefact" in the
 * header. Eight, because that is where the measurement stopped paying: the
 * inharmonic content of a note at the shipped master volume falls from about
 * four to ten per cent of the audible band to a fifth of one per cent, and
 * sixteen takes it to a seventh. It costs eight comparisons a sample on a
 * desktop, which is nothing, and it is the only number here that a listener
 * would notice being wrong. */
#define OVERSAMPLE  8
/* Well under full scale: these are square waves, and several of them at once. */
#define OUTPUT_GAIN 0.8

/* A piezo cannot reproduce DC, and duty is amplitude here -- so without this
 * the envelope of a chime would arrive as a drifting offset rather than as
 * loudness, and every duty step would read as a thump. One pole at 100 Hz. */
#define HIGHPASS_HZ 100.0

/* Everything except the source and the slots is the same for both engines, and
 * so is every line of audio_cb() -- the three accessors below are what keeps it
 * that way. The alternative was a second copy of the SDL device, the highpass
 * and the pulse generator in a second file, which is a much worse trade than
 * forty guarded lines. */
struct render_s
{
    uint8_t master;

    /* Where the walk has got to. `slot_end` is in samples from the start of
     * the chime, which is the only clock in here. */
    uint32_t pos;
    uint32_t slot_end;
    uint32_t frame_t_ms;
    uint16_t frame_ms;

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
    const struct beeper_seq_s *seq;
    struct beeper_slot_s       slot;
#else
    const struct beeper_chime_s *chime;
    struct beeper_slot_s         slots[BEEPER_VOICES_MAX];
    uint8_t                      count;
    uint8_t                      index;
#endif

    double phase; /* 0..1 through the current period, advanced OVERSAMPLE
                   * times per output sample */
    double hp_x;  /* the high-pass filter's last input and output */
    double hp_y;
};

static SDL_AudioDeviceID device;
static struct render_s   render;
static double            hp_alpha;

static uint32_t samples_for(uint16_t ms)
{
    uint32_t n = ((uint32_t)ms * RATE) / 1000u;

    /* A frame is never zero samples long: the walk advances on the slot
     * boundary, and a boundary that is already behind us would spin. */
    return (n == 0) ? 1u : n;
}

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

static bool render_playing(const struct render_s *r) { return r->seq != NULL; }
static void render_stop(struct render_s *r) { r->seq = NULL; }

/* One tone at a time, so the slot is always the one there is. */
static const struct beeper_slot_s *render_slot(const struct render_s *r)
{
    return &r->slot;
}

/* Pull the frame after this one. Returns false when the tune is over.
 *
 * On the engine's own five-millisecond grid, NOT on a finer one, and that is
 * deliberate rather than lazy. The oscillator phase below is carried at sample
 * resolution, but the *parameters* -- the swept pitch, the envelope, the two
 * LFOs -- are re-evaluated exactly as often as the panel's task re-evaluates
 * them. Sampling them per sample here would give the simulator a smoother
 * vibrato than the hardware has, and that is the one direction a simulator must
 * not be wrong in: it would flatter the panel, and a table tuned against it
 * would arrive on real glass sounding stepped.
 *
 * The phase carrying across a frame boundary is the other half of the same
 * argument, and it is what the hardware does -- see the note in the mixer's
 * branch below, which applies here word for word. */
static bool render_advance(struct render_s *r)
{
    r->frame_t_ms += r->frame_ms;
    r->frame_ms = beeper_seq_frame(r->seq, r->frame_t_ms, r->master, &r->slot);

    if (r->frame_ms == 0)
        return false;

    r->slot_end += samples_for(r->frame_ms);

    return true;
}

#else

static bool render_playing(const struct render_s *r) { return r->chime != NULL; }
static void render_stop(struct render_s *r) { r->chime = NULL; }

/* A frame with no voices sounding is a rest, and a rest programs nothing. */
static const struct beeper_slot_s *render_slot(const struct render_s *r)
{
    return (r->count != 0) ? &r->slots[r->index] : NULL;
}

/* Take the next slot of the current frame, or pull the frame after it.
 * Returns false when the chime is over. */
static bool render_advance(struct render_s *r)
{
    /* The phase deliberately carries across a slot boundary rather than
     * restarting.
     *
     * That is what the hardware does, and it is worth writing down because the
     * obvious guess is the other way round: ledc_set_freq() looks like it
     * would reset the counter, and it does not. It calls ledc_set_timer_params(),
     * which writes the divider and latches it at the next overflow -- the
     * counter itself is only reset by ledc_timer_rst(), which nothing in this
     * firmware calls. So a retune changes the rate and leaves the phase alone,
     * and this accumulator does the same.
     *
     * It is audible, and not in a good way: with phase carried, each voice of
     * a chord spreads into a pair of components either side of its nominal
     * pitch. That is a real property of interleaving on this hardware, and the
     * simulator's job is to predict the panel rather than to flatter it. */

    if (r->count != 0 && (uint8_t)(r->index + 1) < r->count)
    {
        r->index++;
        r->slot_end += samples_for(BEEPER_SLOT_MS);

        return true;
    }

    r->frame_t_ms += r->frame_ms;
    r->frame_ms =
        beeper_mixer_frame(r->chime, r->frame_t_ms, r->master, r->slots, &r->count);

    if (r->frame_ms == 0)
        return false;

    r->index = 0;
    /* One voice is not interleaved, so its slot is the whole frame -- exactly
     * as beeper_control.cpp does it on the panel. */
    r->slot_end += samples_for((r->count <= 1) ? r->frame_ms : BEEPER_SLOT_MS);

    return true;
}

#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    struct render_s *r       = (struct render_s *)userdata;
    int16_t         *out     = (int16_t *)stream;
    int              samples = len / (int)sizeof(int16_t);

    for (int i = 0; i < samples; i++)
    {
        double level = 0.0;

        while (render_playing(r) == true && r->pos >= r->slot_end)
        {
            if (render_advance(r) == false)
            {
                render_stop(r);
                break;
            }
        }

        const struct beeper_slot_s *slot =
            (render_playing(r) == true) ? render_slot(r) : NULL;

        if (slot != NULL)
        {
            /* The duty the panel would program, as a fraction of the period:
             * BEEPER_LEVEL_MAX maps to the 50 % that is a pulse train's
             * acoustic peak. Generated as a pulse and not as a scaled sine
             * because the harmonics of a 12 % and a 50 % pulse are completely
             * different, and that difference is the whole point of the volume
             * setting having somewhere to go.
             *
             * Sampled OVERSAMPLE times and averaged, which is the same thing as
             * asking what fraction of this output sample's period the pulse was
             * actually high -- see the header. A single comparison per sample
             * is what put a 588 Hz tone under a 3951 Hz note. */
            double duty = ((double)slot->level / (double)BEEPER_LEVEL_MAX) * 0.5;
            double step = (double)slot->freq / (double)(RATE * OVERSAMPLE);
            unsigned high = 0;

            for (int sub = 0; sub < OVERSAMPLE; sub++)
            {
                r->phase += step;
                r->phase -= floor(r->phase);

                if (r->phase < duty)
                    high++;
            }

            level = (double)high / (double)OVERSAMPLE;
        }

        if (render_playing(r) == true)
            r->pos++;

        double y = hp_alpha * (r->hp_y + level - r->hp_x);

        r->hp_x = level;
        r->hp_y = y;

        double scaled = y * OUTPUT_GAIN * 32767.0;

        if (scaled > 32767.0)
            scaled = 32767.0;
        else if (scaled < -32768.0)
            scaled = -32768.0;

        out[i] = (int16_t)scaled;
    }

    (void)userdata;
}

bool port_beeper_init(void)
{
    if (device != 0)
        return true;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        /* Not fatal, and not an abort: a machine with no sound card, or a
         * headless CI runner, should still run the simulator. */
        ESP_LOGW(TAG, "no audio: %s", SDL_GetError());

        return false;
    }

    SDL_AudioSpec want;
    SDL_AudioSpec have;

    SDL_zero(want);
    want.freq     = RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = BUFFER;
    want.callback = audio_cb;
    want.userdata = &render;

    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    if (device == 0)
    {
        ESP_LOGW(TAG, "no audio device: %s", SDL_GetError());

        return false;
    }

    double dt = 1.0 / (double)RATE;
    double rc = 1.0 / (2.0 * M_PI * HIGHPASS_HZ);

    hp_alpha = rc / (rc + dt);

    /* Left running: the callback emits silence when there is no chime, and
     * pausing between chimes would cost a device start on every blip. */
    SDL_PauseAudioDevice(device, 0);

    ESP_LOGI(TAG, "beeper on SDL audio, %d Hz", have.freq);

    return true;
}

/* Never reached on this target: port_beeper_render*() answers yes, so
 * beeper_control never walks the steps itself here. */
void port_beeper_tone(uint16_t freq, uint16_t level)
{
    (void)freq;
    (void)level;
}

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
bool port_beeper_render_seq(const struct beeper_seq_s *seq, uint8_t master)
#else
bool port_beeper_render(const struct beeper_chime_s *chime, uint8_t master)
#endif
{
    if (device == 0)
        return false;

    SDL_LockAudioDevice(device);

    memset(&render, 0, sizeof(render));

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
    render.seq = seq;
#else
    render.chime = chime;
#endif
    render.master = master;

    /* Prime the walk with the frame at t = 0. slot_end starts at zero, so the
     * callback's first sample pulls it through render_advance() -- which is
     * also what makes an empty chime end immediately rather than hang. */
    render.frame_ms = 0;

    SDL_UnlockAudioDevice(device);

    /* Yes: beeper_control waits out the chime's length rather than walking it,
     * which keeps the two targets serialising chimes the same way. */
    return true;
}

/* Drop the walk, wherever it had got to.
 *
 * The panel's version of this is nearly nothing, because there the frames are
 * walked by a task that can simply stop; here the tune was handed over whole
 * and the audio callback owns it, so this is the only way back.
 *
 * Under the device lock, because render is exactly the structure audio_cb()
 * reads from another thread, and the whole of that structure's contract is
 * that only the callback touches it while the device is unlocked. Clearing it
 * through render_stop() rather than by hand is what keeps the two engines'
 * different notions of "playing" in one place. */
void port_beeper_stop(void)
{
    if (device == 0)
        return;

    SDL_LockAudioDevice(device);
    render_stop(&render);
    SDL_UnlockAudioDevice(device);
}
