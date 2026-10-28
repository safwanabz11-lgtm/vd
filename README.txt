BUILD
 1. Copy the official Zygisk API header to src/zygisk.hpp
    (github.com/topjohnwu/zygisk-module-sample -> module/jni/zygisk.hpp)
 2. ANDROID_NDK=/path/to/ndk ./build.sh   -> badhon_game_profile.zip
USE
 - Flash zip, enable Zygisk, reboot.
 - Put game packages in apps.txt, choose profile in config.txt.
 - Action button = random profile (new seed) + restarts target apps.
 - Check: logcat -s BadhonGP
LIMITS
 - Java Build.* (so WebView UA / Client Hints) is the reliable part.
 - Native hooks (__system_property_get, glGetString) only cover libs already loaded
   at app start; engines that dlopen later may still see the real GPU string.
 - Real CPU cores / RAM / performance are not changed.
WEBUI
 - module/webroot/index.html (KernelSU/APatch manager, or KSUWebUI Standalone / MMRL for Magisk)
 - Select apps, pick profile, Save & Apply. Diagnose button shows why a module is not loading.
 - To add WebUI without rebuilding: copy index.html to /data/adb/modules/badhon_game_profile/webroot/
