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
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
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

        // Hardware volume rocker should drive the media stream (our audio
        // output), not the ringer.
        setVolumeControlStream (android.media.AudioManager.STREAM_MUSIC);
    }

    @Override
    protected void onNewIntent (Intent intent)
    {
        super.onNewIntent (intent);
        setIntent (intent);
    }

    // NAM Player performance guard: while the rig is front-most, nothing may
    // interfere with a live performance. Exclusive audio focus pauses other
    // apps' media; Do Not Disturb (when the user has granted notification
    // policy access) silences notification/ring sounds. Both are released
    // and the previous DND state restored the moment the app leaves the
    // foreground. Focus loss is deliberately ignored — a guitar amp never
    // stops because another app wants the speaker.
    private AudioFocusRequest focusRequest;
    private int previousInterruptionFilter = NotificationManager.INTERRUPTION_FILTER_ALL;
    private boolean dndApplied = false;

    @Override
    protected void onResume()
    {
        super.onResume();

        AudioManager am = (AudioManager) getSystemService (Context.AUDIO_SERVICE);
        if (focusRequest == null)
            focusRequest = new AudioFocusRequest.Builder (AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes (new AudioAttributes.Builder()
                    .setUsage (AudioAttributes.USAGE_MEDIA)
                    .setContentType (AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                .build();
        am.requestAudioFocus (focusRequest);

        NotificationManager nm = (NotificationManager) getSystemService (Context.NOTIFICATION_SERVICE);
        if (nm.isNotificationPolicyAccessGranted())
        {
            previousInterruptionFilter = nm.getCurrentInterruptionFilter();
            // If a force-kill skipped onPause, "current" may be our own
            // leftover total-silence — never adopt that as the restore state.
            if (previousInterruptionFilter == NotificationManager.INTERRUPTION_FILTER_NONE)
                previousInterruptionFilter = NotificationManager.INTERRUPTION_FILTER_ALL;
            try
            {
                nm.setInterruptionFilter (NotificationManager.INTERRUPTION_FILTER_NONE);
                dndApplied = true;
            }
            catch (Exception ignored) {}
        }
        else
        {
            // One-time ask: open the system screen where the user can grant
            // Do Not Disturb access (silences notification dings on stage).
            SharedPreferences prefs = getSharedPreferences ("nam_performance", MODE_PRIVATE);
            if (! prefs.getBoolean ("dnd_prompted", false))
            {
                prefs.edit().putBoolean ("dnd_prompted", true).apply();
                try { startActivity (new Intent (Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS)); }
                catch (Exception ignored) {}
            }
        }
    }

    @Override
    protected void onPause()
    {
        if (dndApplied)
        {
            NotificationManager nm = (NotificationManager) getSystemService (Context.NOTIFICATION_SERVICE);
            try { nm.setInterruptionFilter (previousInterruptionFilter); }
            catch (Exception ignored) {}
            dndApplied = false;
        }
        AudioManager am = (AudioManager) getSystemService (Context.AUDIO_SERVICE);
        if (focusRequest != null)
            am.abandonAudioFocusRequest (focusRequest);
        super.onPause();
    }
}
