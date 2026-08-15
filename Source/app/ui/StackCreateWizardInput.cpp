#include "app/ui/StackCreateWizard.h"
#include "app/ui/NamLookAndFeel.h"

// StackCreateWizard's press/drag/tap state machine, split from
// StackCreateWizard.cpp for the 400-line new-file cap. Gear mutation lives
// in StackCreateWizardGear.cpp; painting in StackCreateWizardPaint.cpp.

void StackCreateWizard::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;
    moved_ = false;
    pressedInContent_ = false;
    pressRegion_ = PressRegion::None;

    if (backRect_.expanded (8).contains (p)) {
        pressRegion_ = PressRegion::Back;
        return;
    }
    if (!footerRect_.isEmpty () && footerBtnRect_.contains (p)) {
        pressRegion_ = PressRegion::Footer;
        return;
    }
    if (step_ != Step::Gallery)
        for (int i = 0; i < kStepCount; ++i)
            if (pillRects_[(size_t)i].contains (p)) {
                pressRegion_ = PressRegion::Pill;
                return;
            }
    if (contentArea_.contains (p)) {
        pressedInContent_ = true;
        pressScrollY_ = scrollY_;
        pressRegion_ = PressRegion::Content;
    }
}

void StackCreateWizard::mouseDrag (const juce::MouseEvent& e) {
    // Movement is tracked for every press regardless of origin region --
    // otherwise a press that starts on chrome (back/pill/footer) and rolls
    // into content never sets `moved_`, and mouseUp below would dispatch it
    // as a tap at the release position. Only a content-origin press also
    // scrolls.
    const auto p = e.getPosition ();
    const int dy = p.y - pressPos_.y;
    if (std::abs (dy) > 8) moved_ = true;
    if (!pressedInContent_ || !moved_) return;
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()),
                             pressScrollY_ - (float)dy);
    repaint (contentArea_);
}

void StackCreateWizard::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    const auto pressRegion = pressRegion_;
    pressedInContent_ = false;
    pressRegion_ = PressRegion::None;
    if (!tap) return;   // gesture crossed the move threshold somewhere -- a drag, never a tap

    // Dispatch only against the region the press ORIGINATED in, and only if
    // the release point is still inside that same region's rect -- a tap
    // that started on the footer and (without ever exceeding the move
    // threshold) ended up over content, or vice versa, does nothing.
    switch (pressRegion) {
        case PressRegion::Back:
            if (backRect_.expanded (8).contains (p)) {
                close ();
                if (onCancel) onCancel ();
            }
            return;
        case PressRegion::Footer:
            if (!footerRect_.isEmpty () && footerBtnRect_.contains (p)) handleFooterTap (p);
            return;
        case PressRegion::Pill:
            if (step_ != Step::Gallery)
                for (int i = 0; i < kStepCount; ++i)
                    if (pillRects_[(size_t)i].contains (p)) {
                        goToStep ((Step)(i + 1));
                        return;
                    }
            return;
        case PressRegion::Content: {
            if (!contentArea_.contains (p)) return;
            const juce::Point<int> cp{ p.x - contentArea_.getX (),
                                       p.y - contentArea_.getY () + (int)scrollY_ };
            handleContentTap (cp);
            return;
        }
        case PressRegion::None:
        default: return;
    }
}
