#include <catch2/catch_all.hpp>
#include "model/Entitlements.h"

TEST_CASE("Entitlements: free tier gates app-native features only") {
    nam::Entitlements e;
    REQUIRE_FALSE(e.isPro());
    // First rig free forever; second requires Pro.
    REQUIRE(e.canSaveRig(0));
    REQUIRE_FALSE(e.canSaveRig(1));
    REQUIRE_FALSE(e.canSaveRig(5));
    // Swipe-card layout (0) free; list/grids (1..3) gated.
    REQUIRE(e.canUseLayout(0));
    REQUIRE_FALSE(e.canUseLayout(1));
    REQUIRE_FALSE(e.canUseLayout(3));
    // First demo track free; the rest gated.
    REQUIRE(e.canUseDemoTrack(0));
    REQUIRE_FALSE(e.canUseDemoTrack(1));
}

TEST_CASE("Entitlements: pro unlocks everything") {
    nam::Entitlements e;
    e.setPro(true);
    REQUIRE(e.isPro());
    REQUIRE(e.canSaveRig(99));
    REQUIRE(e.canUseLayout(3));
    REQUIRE(e.canUseDemoTrack(33));
}

TEST_CASE("Entitlements: revoke re-locks without deleting anything") {
    nam::Entitlements e;
    e.setPro(true);
    e.setPro(false);
    REQUIRE_FALSE(e.isPro());
    // Rigs beyond the free one are read-only-locked by callers; the policy
    // class only answers canSaveRig — there is deliberately NO API to cap
    // or delete saves (TONE3000-parity regression pin).
    REQUIRE_FALSE(e.canSaveRig(2));
    REQUIRE(e.canSaveRig(0));
}
