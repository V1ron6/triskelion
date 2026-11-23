# The native bridge looks these up by name (JNI), so they must not be renamed or stripped.
-keep class com.triskelion.sdk.NativeBridge { *; }
-keep class com.triskelion.sdk.NativeBridge$* { *; }
