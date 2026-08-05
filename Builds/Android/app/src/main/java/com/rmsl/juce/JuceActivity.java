/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

package com.rmsl.juce;

import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
import static android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.content.Intent;
import android.view.View;

//==============================================================================
public class JuceActivity   extends Activity
{
    // NOTE (NAM Player, Phase 5a): the upstream JUCE JuceActivity also declares
    // `appNewIntent`/`appOnResume` native methods and calls them from
    // onNewIntent/onResume. Those C++ callbacks are only registered when
    // JUCE_PUSH_NOTIFICATIONS_ACTIVITY is defined (i.e. push-notifications /
    // in-app-purchases builds). This minimal app enables neither, so the native
    // methods are never registered -- calling them crashed with
    // UnsatisfiedLinkError. They are removed here to keep the Java in sync with
    // the compiled native library. Re-add them (and the native decls) if push
    // notifications / IAP are ever enabled.

    // NAM Player: go edge-to-edge so the app background fills behind the
    // (translucent) system bars, and the native app layer insets its own
    // controls by the safe-area (Desktop safeAreaInsets) so nothing sits under
    // the status/navigation bars. This keeps the system nav bar visible and
    // tappable while the app still fills the screen.
    @SuppressWarnings ("deprecation")
    private void initEdgeToEdge()
    {
        if (30 <= Build.VERSION.SDK_INT)
            getWindow().setDecorFitsSystemWindows (false);
    }

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        initEdgeToEdge();

        super.onCreate (savedInstanceState);
    }

    @Override
    protected void onNewIntent (Intent intent)
    {
        super.onNewIntent (intent);
        setIntent (intent);
    }
}
