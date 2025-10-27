# Kassiopeia Eckdaten

These are valid on a per channel basis.

### PWM Mode

- Mode Switch pressed **IN**
- Freq ~30Khz
- Width min: 2.5% -> basically 0% for any motor because of momentum
  - Potentially use different curve (via LUT) that acts more linearly
- [pot position] + [CV value] + [MIDI CC] will indivially increase the width to 100% and are internally always added up

### Trigger Mode

- Mode Switch pressed **OUT**
- Pulse length min: 20ms
- Pulse length max: 50ms (for CV) & 70ms (for MIDI)
- [pot position] and [note on velocity] determine the pulse length and are **not** internally added together (a second trigger during an ongoing pulse will be killed by the first one)
- Triggered by CV via [rising edge] and by MIDI via [note on] message
- both a [falling edge] and a [note off] message will kill the pulse signal prematurely if they are fast enough -> playback with higher frequencies

### MIDI

- Accepts Type A **and** B TRS Midi
- MIDI Notes 0, 2, 4 and 5 are mapped for trigger per channel each
- MIDI CC 20, 21, 22 and 23 are mapped for pwm per channel each
- Factory setting accepts notes and CC on every incoming channel

### MIDI Learn

To assign new values for midi trigger/pwm messages, press the [midi learn] button while powering the device on. The [power led] should be blinking now. Now you can play the desired [midi notes] on your synthesizer or DAW (one after another). You can see with the LEDs which [midi note] will be assigned to which [kassiopeia channel]. Same goes for CC. You need to also assign every last CC to the [kassiopeia channels]. Only then will the Kassiopeia go back to normal mode with the newly assigned midi values. 

Multiple values for each [kassiopeia channel] are not valid and will be ignored -> not possible to assign one [midi note] or [midi cc] from the same [midi channel] to two or more [kassiopeia channels].

Please be aware that [MIDI Learn] also records the [midi channels] on which the incoming messages came in. This means in theory you could assign the same [midi note] to each individual [kassiopeia channel], but on a different [midi channel].

### Factory Reset

To reset the device back to factory settings (and omni-channel midi) press the [Midi Learn button] while powering on the device and once the [power led] is blinking, press the [Midi Learn button] again. Now the device has resetted back to factory settings and will react to [midi notes] 0, 2, 4 and 5 on every [midi channel].