package com.ps2x.runner;

/**
 * Native rendering activity. It has no Java behaviour: NativeActivity loads
 * libps2EntryRunner.so, whose ANativeActivity_onCreate (raylib) calls main().
 */
public final class GameActivity extends android.app.NativeActivity {
}
