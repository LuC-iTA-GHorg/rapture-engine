#pragma once

// WP-7: archive writer tests.  `<exe> --rrs-test --roundtrip [FOLDER]`  (dispatched from RunRrsTest, tests/RrsTest.cpp).
//   1. synthetic self-test (no game data needed): DIB / ADPCM / MIDI encoders, Image build -> Save -> Open.
//   2. every *.rrs found under FOLDER (default ".") is loaded into rrs::Image and re-serialized (must be byte-identical),
//      then rebuilt from its decoded content with the encoders (DIB RLE16, ADPCM, compact MIDI, group/string/dibref builders).
// Returns 0 when everything passed.
int RunRrsRoundtrip(int argc, char** argv);
