# The beeper

The panel has one piezo, on one GPIO, driven by one LEDC timer -- and the
frequency is a property of the timer. So there is exactly one tone available at
any instant, and that one tone is the entire budget for eighteen themed sounds
across three theme families.

There are two engines here, and they spend that budget differently. Neither is
free, and the choice between them is a taste one rather than a correctness one.

## The two engines

**The sequencer** (`main/control/beeper_seq.{h,c}`, the default) plays one note
at a time and puts everything that would have gone on a second voice into the
note instead: a four-stage envelope, a sweep that can walk the period rather
than the frequency, a vibrato, a tremolo, and a repeat count. A chord is spelled
as an arpeggio. It steps every five milliseconds, so a sounding tune is two
hundred wakeups a second.

**The mixer** (`main/control/beeper_mixer.{h,c}`) plays real chords, by
time-multiplexing: each of up to three voices holds the channel for two
milliseconds in turn, fast enough that the ear fuses them. What it costs is a
grain on every chord -- chopping a tone at 250 Hz puts sidebands either side of
it -- a floor of about a kilohertz under anything stacked, up to five hundred
timer reconfigurations a second, and per-note expression limited to three fixed
envelope shapes.

The honest summary is that the sequencer is better at *notes* and the mixer is
better at *intervals*. Which matters depends on the family:

| family | under the sequencer | under the mixer |
| --- | --- | --- |
| Slate | barely different -- ten of its eighteen were single voices already | |
| Reticle | better: its swells finally get the vibrato they were reaching for | |
| LCARS | | better where it stacks: the toggles, the acknowledgements and the standing alert are intervals, and an arpeggio is not a chord |

LCARS is the reason the mixer is still here rather than deleted -- though less
so than it was, because the rework described under *The three families* took
most of that family's sweeps out and put stepped figures in their place, and a
stepped figure is a thing one voice can say.

## Choosing one

`idf.py menuconfig` → **OhEzTouch** → *Beeper engine*:

- `CONFIG_OHEZ_BEEPER_ENGINE_SEQ` (*Expressive -- melodies, envelopes, sweeps,
  vibrato and tremolo*, the default)
- `CONFIG_OHEZ_BEEPER_ENGINE_MIXER` (*Polyphonic -- up to three voices
  interleaved on the one piezo*)

It is a build-time choice because each engine has its own note format and
therefore its own tables: the fifty-four sounds are written out twice, once for
each, and a panel carries only the set it plays. A runtime switch would put both
sets in flash for a choice nobody changes twice.

The Lanbon L8 has no buzzer, so on that board this decides nothing.

## What a tune is

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

Eight bytes for the tune, which is exactly what a chime was, so the four-deep
request queue still carries one by value -- a tune either plays or does not,
rather than having its tail dropped a note at a time.

`repeat` living on the note rather than on the tune is what makes a trill and an
alert cadence one line each. That it treats 0 and 1 alike is deliberate: a note
left half-written by a table that forgot a field should be *audible*, because
silent is the failure mode nobody notices.

## Envelopes

Four stages -- attack, decay, sustain, release -- and the note's duration is the
budget, not an event that arrives. There is no key-off on a piezo: a sound's
length is known before it starts, so the release is the tail at the end of the
note rather than a reaction to anything.

The three stage lengths are **percentages of the note, with a cap in
milliseconds**, and that pair is the whole reason one row describes the whole
interface. A twelve-millisecond contact tick and a four-hundred-millisecond boot
note have to sound like the same instrument; a fixed five-millisecond attack is
a sixth of the first and an eightieth of the second. The cap is the other end of
the same problem: sixty per cent of four hundred milliseconds is a
two-hundred-and-forty-millisecond fade-in, which is not a swell.

`FLAT`, `CLICK`, `PLUCK`, `PAD`, `STAB`, `BELL`, `SWELL`, `BLOOM`. The mixer's
three shapes are three of those rows and nothing was lost.

## Effects

`sweep` is `f_start` → `f_end` over the note, either linear in hertz or as a
**glide** that interpolates the *period*. A linear sweep in hertz spends most of
its time at the top -- 1200 to 2800 crosses its first octave in a third of the
note -- which is why the old chirps sounded top-heavy. The glide is geometric to
within a per cent, costs one divide, and is also what the hardware would give
you: LEDC's frequency is a divider, so a linear walk of the divider is exactly
this.

**Vibrato** is a pitch LFO with its depth in per mille *of the carrier*, so one
row behaves the same at 1.2 kHz and at 3.9 kHz. A fixed excursion in hertz would
be a siren at the bottom of the band and a shimmer at the top, and every row
would need a twin. It is also the one thing the mixer explicitly cannot do: its
header notes that two voices a few hertz apart will not beat, because phase is
not carried across a slot boundary.

**Tremolo** is an amplitude LFO and it only ever goes *down*. Upward tremolo
would push a note past the peak its envelope asked for, and the master volume
has no headroom above 100 to give it back.

Both LFOs are triangles and both restart at the start of every note -- so a
trill is three strikes that each catch the vibrato in the same place, rather
than three that each catch it somewhere different.

`NONE`, `GLIDE`, `SHIMMER`, `WOBBLE`, `SIREN`, `BREATHE`, `PULSE`, `CHIRP`.

### The two rules about rate

There is a ceiling and a floor, and both are host tests rather than paragraphs.

`BEEPER_SEQ_LFO_MAX_CHZ` is the ceiling: above about 25 Hz the wobble aliases
against the five-millisecond step, and the simulator -- which evaluates at audio
resolution -- would hear a smooth one where the panel hears a stepped one. It
also bounds the LFO's own multiply.

The floor is that **a note carrying an LFO is at least one period of it long**.
An LFO slower than its note is not an ornament, it is a pitch bend: the note ends
partway up the first rise and never comes back. This caught three of the six
effect rows the first time the shipped tables were walked and measured, and it
is invisible in the table -- the note looks like it has a vibrato and the effect
row looks like a vibrato.

## The three families

The tables are `main/ui/ui_beep_tables_seq.cpp` and `main/ui/ui_beep_tables.cpp`,
one per engine, and the design policy is the same for both:

- Frequencies live between about 1.1 and 3.9 kHz, because that is where a small
  piezo is loud. The **error** sounds break that deliberately -- being hard to
  ignore matters more than being loud, and a klaxon that sounds like the rest of
  the interface is not a klaxon. `may_go_low()` in the tests is where that
  exemption is written down, and a second test checks the exemption is still
  being used.
- Immediate feedback is over in 150 ms; anything else in 700 ms. Past that a
  press sound stops being an acknowledgement and starts being an echo.
- The contact layer -- `UI_SOUND_PRESS` -- is about a third of the level of
  everything else. The layering policy it belongs to is in `main/ui/ui_beep.hpp`
  and is worth reading before touching any of this.

### LCARS, and what it is imitating

The family is named after a console, and the table was reworked to sound like
one rather than like science fiction in general. What that came down to is four
properties, all of them structural, because structure is the part a piezo can
carry and timbre is the part it cannot:

- **Stepped, not swept.** A blip on that console is two to four discrete tones
  butted together, fifteen to forty milliseconds each -- not a glide. The table
  used to spend its two most-used sounds on a note swept most of the band in
  ninety milliseconds, which is a scanning noise rather than a panel answering
  a finger. Two sweeps survive: the hail rises because a hail rises, and the
  klaxon whoops because a klaxon whoops.
- **Falling as often as rising.** The most recognisable console sound is a
  two-tone that drops. The old table rose nearly everywhere, because rising
  reads as affirmative and the vocabulary is mostly affirmative; the drop now
  belongs to the two gestures that are not going anywhere -- switching
  something off, and waking the panel.
- **Short.** Everything but the three alerts and the door is inside 150 ms,
  which is a long way under what the policy allows. The policy is a ceiling and
  this family sits well below it on purpose.
- **One register.** Between about 1.5 and 3.5 kHz, leaning on rhythm and
  contour to be told apart rather than on range. The alerts break out of it
  downwards, which is what alerts do.

Nobody should read the result as a recording. It is a square wave from a piezo
and the sounds it is imitating are sampled, layered and reverberant; what
carries across is contour, rhythm, register and interval, and that is most of
what makes a sound recognisable but it is not all of it. The table comments say
which of them each sound is spending.

### The door chime

`UI_SOUND_DOOR_CHIME` is the eighteenth, and the only one with no call site. No
gesture on a touchscreen means "somebody is at the door", so it is reachable
over MQTT and nowhere else -- see *Playing a sound* in the README. It is in the
vocabulary rather than bolted on beside it so that what a door sounds like is a
theme's decision, the same as everything else, and so that the table tests hold
it to the same policy. Each family answers it differently: Slate strikes two
tones and lets them ring, LCARS does the same but is the one sound in that
family that is allowed a bell rather than a stab, and Reticle blooms in and
thins out.

A test in each suite asserts it is not a copy of its neighbour, which is a check
the other seventeen do not need: they are all reachable by using the panel, and
a wrong one would be noticed.

Under the mixer there is one more: nothing below `BEEPER_POLY_MIN_HZ` is ever
stacked, because a two-millisecond slot down there is less than two cycles and
the pitch dissolves into the slot rate. The sequencer has no polyphony and so no
such rule; what replaces it is the vibrato excursion, which can swing a note
written inside the band out of it.

## Hearing it

See [Hearing the panel](../README.md#hearing-the-panel). The simulator
synthesises the pulse train the LEDC channel would produce, walking the same
frame function the panel walks -- there is no second copy of the synthesis to
drift out of step.

It re-evaluates the engine's parameters on the engine's own five-millisecond
grid and carries only the oscillator phase at sample resolution. Sampling the
parameters per sample would give the simulator a smoother vibrato than the
hardware has, which is the one direction a simulator must not be wrong in: it
would flatter the panel, and a table tuned against it would arrive on real glass
sounding stepped.

## Tests

`test/host/` builds both engines and both sets of tables into one binary:

- `test_beeper_mixer.cpp` / `test_beeper_seq.cpp` -- the arithmetic. Sweeps that
  would wrap, envelopes that would underflow, LFOs that would overflow, frame
  walks that would not terminate, and in both files the assertion that a
  foreground note at the shipped master volume of 25 still lands on sixty-three
  counts of duty.
- `test_ui_beep_chimes.cpp` / `test_ui_beep_tunes.cpp` -- the tables, against the
  shared policy in `test_ui_beep_policy.hpp`. The chime suite also covers
  `ui_sound_from_name()`, which is what turns a `sound/set` payload back into a
  sound: that lookup has no other front end, and a name that stopped matching
  would be a log line on a panel nobody is watching.

Both sets are checked even though a panel ships one, because the set that is not
selected is exactly the one nobody would notice going stale. That is why the two
table files export different type and symbol names: they have to link side by
side.
