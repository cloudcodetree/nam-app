#pragma once
#include <string>
#include <vector>
#include "model/StackModel.h"

// The 3 built-in "start from a rig" templates for the Stack creation
// wizard's step-0 gallery (StackCreateWizard.cpp). Each is a nam::Stack
// literal with EMPTY toneIds -- named placeholders only, not downloadable
// tones -- so cloning one gives the user pre-built structure (amp channels,
// pedals, cab, an FS map on the first 3 switches) to swap real gear into via
// Stack detail EDIT afterward; switch D is always left for "Tap tempo",
// which has no chain item to carry an fs number.
// Data only, header-only (no .cpp, no CMake registration needed).
namespace nam::templates {

struct Template {
    std::string name;    // gallery card title + the cloned Stack's name
    std::string genre;   // gallery card tag, e.g. "Classic Rock"
    Stack stack;
};

inline ChainItem placeholderAmp (std::string title, std::vector<std::string> channelNames, int fs) {
    ChainItem it;
    it.type = GearType::Amp;
    it.title = title;
    it.gearTag = "amp";
    it.format = "nam";
    it.fs = fs;
    for (auto& n : channelNames) it.channels.push_back ({ "", n });
    return it;
}

inline ChainItem placeholderPedal (std::string title, int fs) {
    ChainItem it;
    it.type = GearType::Pedal;
    it.title = title;
    it.gearTag = "pedal";
    it.format = "nam";
    it.fs = fs;
    return it;
}

inline ChainItem placeholderCab (std::string title) {
    ChainItem it;
    it.type = GearType::Cab;
    it.title = title;
    it.gearTag = "cab";
    it.format = "ir";
    return it;
}

inline Template plexiCrunch () {
    Template t;
    t.name = "Plexi Crunch";
    t.genre = "Classic Rock";
    t.stack.name = t.name;
    t.stack.chain = {
        placeholderAmp ("Plexi Lead", { "Clean", "Crunch" }, 1),
        placeholderPedal ("Tube Screamer", 2),
        placeholderPedal ("Analog Boost", 3),
        placeholderCab ("4x12 Plexi Cab"),
    };
    return t;
}

inline Template modernMetal () {
    Template t;
    t.name = "Modern Metal";
    t.genre = "Metal";
    t.stack.name = t.name;
    t.stack.chain = {
        placeholderAmp ("Modern High Gain", { "Rhythm", "Lead" }, 1),
        placeholderPedal ("Tight Gate", 2),
        placeholderPedal ("OD Boost", 3),
        placeholderCab ("4x12 V30"),
    };
    return t;
}

inline Template cleanPlatform () {
    Template t;
    t.name = "Clean Platform";
    t.genre = "Clean / Ambient";
    t.stack.name = t.name;
    t.stack.chain = {
        placeholderAmp ("Clean Twin", { "Clean", "Bright" }, 1),
        placeholderPedal ("Chorus", 2),
        placeholderPedal ("Ambient Delay", 3),
        placeholderCab ("2x12 Clean Cab"),
    };
    return t;
}

// Fresh instances each call -- no shared mutable state (the caller clones by
// value and reassigns uids when a card is picked; see
// StackCreateWizard::pickTemplate).
inline std::vector<Template> builtins () {
    return { plexiCrunch (), modernMetal (), cleanPlatform () };
}

}   // namespace nam::templates
