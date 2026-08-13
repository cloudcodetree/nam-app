#pragma once

namespace nam {

// Monetization policy (JUCE-free). TONE3000-parity rule: there is NO API
// here for gating saves/downloads/favorites — only app-native features.
// Spec: docs/superpowers/specs/2026-08-12-freemium-pro-unlock-design.md
class Entitlements {
public:
    void setPro(bool pro) { pro_ = pro; }
    bool isPro() const { return pro_; }

    // First rig is free forever; more require Pro.
    bool canSaveRig(int existingRigs) const { return pro_ || existingRigs < kFreeRigs; }
    // Layout 0 = swipe cards (free); 1..3 = list/grids (Pro).
    bool canUseLayout(int layoutMode) const { return pro_ || layoutMode == 0; }
    // DI audition tracks: first free, rest Pro.
    bool canUseDemoTrack(int index) const { return pro_ || index < kFreeDemoTracks; }

    static constexpr int kFreeRigs = 1;
    static constexpr int kFreeDemoTracks = 1;

private:
    bool pro_ = false;
};

}   // namespace nam
