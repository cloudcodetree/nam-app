#pragma once

// The complete TONE3000 web-player DI catalog (MIT-licensed, from
// tone-3000/neural-amp-modeler-wasm ui/public/inputs). The first four are
// bundled in the APK (BinaryData resource named); the rest download on
// first use and are cached locally. File names must match the repo exactly
// (note "Progression -  Guitar.wav" really has two spaces).
namespace nam { namespace demo {

struct TrackDef {
    const char* display;         // dropdown label (TONE3000's name)
    const char* fileName;        // exact file name in the repo
    const char* binaryResource;  // BinaryData name when bundled, else nullptr
};

inline constexpr TrackDef kTracks[] = {
    { "Mayer - Guitar",        "Mayer - Guitar.wav",        "di_chords_wav" },
    { "Slide Lead - Guitar",   "Slide Lead - Guitar.wav",   "di_lead_wav" },
    { "Metalcore - Guitar",    "Metalcore - Guitar.wav",    "di_chugs_wav" },
    { "Smokin' - Bass",        "Smokin' - Bass.wav",        "di_bass_wav" },
    { "Brit - Guitar",         "Brit - Guitar.wav",         nullptr },
    { "Celestial - Guitar",    "Celestial - Guitar.wav",    nullptr },
    { "Cream - Guitar",        "Cream - Guitar.wav",        nullptr },
    { "Decapitated - Guitar",  "Decapitated - Guitar.wav",  nullptr },
    { "Downtown - Bass",       "Downtown - Bass.wav",       nullptr },
    { "Drivin' - Bass",        "Drivin' - Bass.wav",        nullptr },
    { "Fast Thrash - Guitar",  "Fast Thrash - Guitar.wav",  nullptr },
    { "Fear - Guitar",         "Fear - Guitar.wav",         nullptr },
    { "Frogger - Bass",        "Frogger - Bass.wav",        nullptr },
    { "Garden - Bass",         "Garden - Bass.wav",         nullptr },
    { "Groove Thrash - Guitar","Groove Thrash - Guitar.wav",nullptr },
    { "Hammer Lead - Guitar",  "Hammer Lead - Guitar.wav",  nullptr },
    { "Harmonics - Guitar",    "Harmonics - Guitar.wav",    nullptr },
    { "Honky - Guitar",        "Honky - Guitar.wav",        nullptr },
    { "Hotrod - Guitar",       "Hotrod - Guitar.wav",       nullptr },
    { "Jazz Hop - Guitar",     "Jazz Hop - Guitar.wav",     nullptr },
    { "Jazz Trot - Guitar",    "Jazz Trot - Guitar.wav",    nullptr },
    { "John - Guitar",         "John - Guitar.wav",         nullptr },
    { "Lunar - Guitar",        "Lunar - Guitar.wav",        nullptr },
    { "Pluck - Guitar",        "Pluck - Guitar.wav",        nullptr },
    { "Pop Punk - Guitar",     "Pop Punk - Guitar.wav",     nullptr },
    { "Power - Guitar",        "Power - Guitar.wav",        nullptr },
    { "Power Thrash - Guitar", "Power Thrash - Guitar.wav", nullptr },
    { "Progression - Guitar",  "Progression -  Guitar.wav", nullptr },
    { "Raid - Guitar",         "Raid - Guitar.wav",         nullptr },
    { "Rollin' - Bass",        "Rollin' - Bass.wav",        nullptr },
    { "Rotary - Guitar",       "Rotary - Guitar.wav",       nullptr },
    { "Smooth - Guitar",       "Smooth - Guitar.wav",       nullptr },
    { "Stroke - Guitar",       "Stroke - Guitar.wav",       nullptr },
    { "Tomb - Guitar",         "Tomb - Guitar.wav",         nullptr },
};

inline constexpr int kNumTracks = (int) (sizeof (kTracks) / sizeof (kTracks[0]));

} } // namespace nam::demo
