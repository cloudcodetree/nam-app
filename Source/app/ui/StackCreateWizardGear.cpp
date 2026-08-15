#include "app/ui/StackCreateWizard.h"
#include <algorithm>
#include <string>

// Gear mutation (steps 1-3: amp channels / pedals / cab), step-4 footswitch
// assignment, and the save/template-pick paths. Split out of
// StackCreateWizard.cpp per the no-god-files rule -- state/layout/input
// handling there was already pushing past 400 lines on its own; see that
// file's header comment for the wizard's overall shape and the
// LibraryEntry-has-no-gear-tag caveat that shapes steps 1/2.
namespace {
nam::ChainItem* findByType (nam::Stack& s, nam::GearType t) {
    for (auto& it : s.chain)
        if (it.type == t) return &it;
    return nullptr;
}
nam::ChainItem* findByUid (nam::Stack& s, const juce::String& uid) {
    for (auto& it : s.chain)
        if (juce::String (it.uid) == uid) return &it;
    return nullptr;
}
}   // namespace

// --- Gear mutation (steps 1-3) -----------------------------------------

void StackCreateWizard::addAmpChannel (const nam::LibraryEntry& e) {
    auto* amp = findByType (draft_, nam::GearType::Amp);
    if (amp == nullptr) {
        nam::ChainItem it;
        it.uid = nam::StackModel::nextUid (draft_);
        it.type = nam::GearType::Amp;
        it.gearTag = "amp";
        it.format = "nam";
        it.toneId = e.id;
        it.title = e.displayName;
        it.channels.push_back ({ e.id, e.displayName });
        it.activeChannel = 0;
        draft_.chain.push_back (std::move (it));
    } else {
        amp->channels.push_back ({ e.id, e.displayName });
    }
    layout ();
    repaint ();
}

void StackCreateWizard::removeAmpChannel (int channelIdx) {
    auto* amp = findByType (draft_, nam::GearType::Amp);
    if (amp == nullptr || channelIdx < 0 || channelIdx >= (int)amp->channels.size ()) return;
    const auto ampUid = juce::String (amp->uid);
    amp->channels.erase (amp->channels.begin () + channelIdx);
    if (amp->activeChannel >= (int)amp->channels.size ())
        amp->activeChannel = juce::jmax (0, (int)amp->channels.size () - 1);
    if (amp->channels.empty ()) {
        draft_.chain.erase (std::remove_if (draft_.chain.begin (), draft_.chain.end (),
                                            [&] (const nam::ChainItem& it) {
                                                return juce::String (it.uid) == ampUid;
                                            }),
                            draft_.chain.end ());
        pruneStaleAssignments ();
    }
    layout ();
    repaint ();
}

void StackCreateWizard::togglePedal (const nam::LibraryEntry& e) {
    for (auto it = draft_.chain.begin (); it != draft_.chain.end (); ++it)
        if (it->type == nam::GearType::Pedal && it->toneId == e.id) {
            draft_.chain.erase (it);
            pruneStaleAssignments ();
            layout ();
            repaint ();
            return;
        }
    nam::ChainItem it;
    it.uid = nam::StackModel::nextUid (draft_);
    it.type = nam::GearType::Pedal;
    it.gearTag = "pedal";
    it.format = "nam";
    it.toneId = e.id;
    it.title = e.displayName;
    draft_.chain.push_back (std::move (it));
    layout ();
    repaint ();
}

void StackCreateWizard::pickCab (const nam::LibraryEntry& e) {
    auto* cab = findByType (draft_, nam::GearType::Cab);
    if (cab == nullptr) {
        nam::ChainItem it;
        it.uid = nam::StackModel::nextUid (draft_);
        it.type = nam::GearType::Cab;
        it.gearTag = "cab";
        it.format = "ir";
        it.toneId = e.id;
        it.title = e.displayName;
        draft_.chain.push_back (std::move (it));
    } else {
        cab->toneId = e.id;
        cab->title = e.displayName;
    }
    layout ();
    repaint ();
}

// --- Step 4: actions + footswitch assignment ----------------------------

std::vector<StackCreateWizard::ActionRow> StackCreateWizard::buildActions () const {
    std::vector<ActionRow> rows;
    if (const auto* amp = ampItem ())
        if (amp->channels.size () > 1)
            rows.push_back (
                { juce::String (amp->uid), juce::String (amp->title) + " channel cycle" });
    for (const auto& it : draft_.chain)
        if (it.type == nam::GearType::Pedal)
            rows.push_back ({ juce::String (it.uid), juce::String (it.title) + " on/off" });
    rows.push_back ({ juce::String (), "Tap tempo" });
    return rows;
}

void StackCreateWizard::autoMapIfNeeded () {
    if (autoMapped_) return;
    autoMapped_ = true;
    switches_ = {};
    int slot = 0;
    for (const auto& a : buildActions ()) {
        if (a.uid.isEmpty ()) continue;   // Tap tempo is placed on D explicitly, below
        if (slot >= 3) break;             // only A, B, C auto-fill from gear
        switches_[(size_t)slot] = { false, a.uid };
        ++slot;
    }
    switches_[3] = { true, {} };   // D always Tap tempo on first entry
    syncFsIntoChain ();
}

void StackCreateWizard::pruneStaleAssignments () {
    // A bound uid can still exist in draft_.chain while the ACTION it was
    // bound to no longer does -- e.g. an amp reduced from 2 channels to 1
    // drops out of buildActions()'s channel-cycle row (which requires
    // channels.size() > 1) without the amp item itself being removed.
    // findByUid alone would miss that and leave a switch "SET" against a
    // dead action, which syncFsIntoChain would then happily persist as an
    // fs on an item nothing maps to anymore. Validate against the CURRENT
    // buildActions() set (uid + action kind, since each uid contributes at
    // most one row) instead of raw chain membership.
    const auto actions = buildActions ();
    const auto stillActionable = [&] (const juce::String& uid) {
        for (const auto& a : actions)
            if (a.uid == uid) return true;
        return false;
    };
    for (auto& sw : switches_) {
        if (sw.tapTempo || sw.uid.isEmpty ()) continue;
        if (!stillActionable (sw.uid)) sw = {};
    }
    syncFsIntoChain ();
}

void StackCreateWizard::armSwitch (int idx) {
    if (idx < 0 || idx > 3) return;
    armedSwitch_ = (armedSwitch_ == idx) ? -1 : idx;
    repaint (contentArea_);
}

void StackCreateWizard::assignArmedTo (const ActionRow& row) {
    if (armedSwitch_ < 0) return;
    // Each action lives on at most one switch -- clear any other switch
    // currently pointing at the same target before reassigning it here.
    for (auto& sw : switches_) {
        if (row.uid.isEmpty ()) {
            if (sw.tapTempo) sw = {};
        } else if (sw.uid == row.uid) sw = {};
    }
    switches_[(size_t)armedSwitch_] =
        row.uid.isEmpty () ? SwitchAssign{ true, {} } : SwitchAssign{ false, row.uid };
    armedSwitch_ = -1;
    syncFsIntoChain ();
    layout ();
    repaint ();
}

void StackCreateWizard::clearArmed () {
    if (armedSwitch_ < 0) return;
    switches_[(size_t)armedSwitch_] = {};
    armedSwitch_ = -1;
    syncFsIntoChain ();
    layout ();
    repaint ();
}

juce::StringArray StackCreateWizard::unmappedActionLabels () const {
    juce::StringArray unmapped;
    for (const auto& a : buildActions ()) {
        bool bound = false;
        for (const auto& sw : switches_)
            if (a.uid.isEmpty () ? sw.tapTempo : sw.uid == a.uid) {
                bound = true;
                break;
            }
        if (!bound) unmapped.add (a.label);
    }
    return unmapped;
}

juce::String StackCreateWizard::warningText () const {
    const auto unmapped = unmappedActionLabels ();
    if (unmapped.isEmpty ()) return {};
    return juce::String (unmapped.size ()) +
           " action(s) won't be foot-switchable: " + unmapped.joinIntoString (", ");
}

void StackCreateWizard::syncFsIntoChain () {
    for (auto& it : draft_.chain) it.fs = 0;
    for (int i = 0; i < 4; ++i) {
        const auto& sw = switches_[(size_t)i];
        if (sw.tapTempo || sw.uid.isEmpty ()) continue;
        if (auto* it = findByUid (draft_, sw.uid)) it->fs = i + 1;
    }
}

// --- Save / template pick ------------------------------------------------

void StackCreateWizard::pickTemplate (int idx) {
    if (idx < 0 || idx >= (int)galleryTemplates_.size ()) return;
    const auto& tmpl = galleryTemplates_[(size_t)idx].stack;
    nam::Stack cloned;
    cloned.name = tmpl.name;
    cloned.routing = tmpl.routing;
    for (auto item : tmpl.chain) {
        item.uid = nam::StackModel::nextUid (cloned);
        cloned.chain.push_back (std::move (item));
    }
    if (onSave) onSave (cloned, {});   // template pick: no toast, per spec
    close ();
}

// No scene editor exists yet (see docs/wiki/decisions.md) -- without this,
// a wizard-built stack saves with zero scenes and PERFORM's SCENES grid is
// empty from the moment it's created. Minimum-viable seed: one Scene per
// amp channel, named after the channel (or "Scene {n}" if the channel has
// no title), so PERFORM has something to switch between immediately. Pedal
// bypass is mirrored from the chain's current (all-on, as built -- nothing
// in the wizard toggles bypass) state rather than hardcoded, so this stays
// correct if that ever changes. Template picks (pickTemplate) are untouched
// -- a template already defines its own scenes.
void StackCreateWizard::seedScenes () {
    draft_.scenes.clear ();
    const auto* amp = ampItem ();
    if (amp == nullptr || amp->channels.empty ()) {
        draft_.activeScene = -1;
        return;
    }
    for (int i = 0; i < (int)amp->channels.size (); ++i) {
        nam::Scene sc;
        const auto& title = amp->channels[(size_t)i].title;
        sc.name = title.empty () ? ("Scene " + std::to_string (i + 1)) : title;
        sc.ampChannel = i;
        for (const auto& it : draft_.chain)
            if (it.type == nam::GearType::Pedal) sc.pedalBypass[it.uid] = it.bypassed;
        draft_.scenes.push_back (std::move (sc));
    }
    draft_.activeScene = 0;
}

void StackCreateWizard::doSave () {
    syncFsIntoChain ();
    seedScenes ();
    const int unmapped = unmappedActionLabels ().size ();
    // The spec copy claims a full A-D map -- only true when nothing is
    // unmapped. Compose a truthful variant otherwise rather than lying.
    // Hex escapes are greedy ("\xE2\x80\x93D" would swallow the 'D' as part
    // of the escape) -- split after the dash so "D" starts fresh.
    const juce::String toast = unmapped == 0
                                   ? juce::String::fromUTF8 ("Saved \xC2\xB7 "
                                                             "A\xE2\x80\x93"
                                                             "D mapped "
                                                             "on your Chocolate")
                                   : juce::String::fromUTF8 ("Saved \xC2\xB7 ") +
                                         juce::String (unmapped) + " action(s) not foot-switchable";
    if (onSave) onSave (draft_, toast);
    close ();
}
