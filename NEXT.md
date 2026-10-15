Get the owner's verdict on v4 ("Augustus dev"): finger-only play now deselects the tool after each confirmed placement, and any two-finger gesture pans/zooms even with a tool selected; pen mode unchanged. Logcat tag `AugustusPen`.

State: branch `spen-mouse` adds `android/augustus/.../StylusSurface.java`, which feeds the stylus to SDL as a mouse instead of a finger (SDL2 2.32.10 drops stylus buttons; touch mode needs a confirm tap for placements). Debug build only, installed alongside the official app (`com.github.Keriew.augustus.debug`). Game files are on the phone at shared storage `Caesar3/` (unpacked from the GOG installer).

Build: `. ~/dev/dev-env.sh; cd android && ./gradlew assembleDebug` — needs SDL2-2.32.10 + SDL2_mixer-2.8.2 sources in `ext/SDL2/` (gitignored, from libsdl.org) and `android/local.properties` with `sdk.dir=~/dev/android-sdk`.

Remotes: `origin` = NullOpStop/augustus (owner's existing PUBLIC GitHub fork, likely also checked out on finn-lt), `upstream` = Keriew/augustus.

Waiting on owner: whether the stylus mapping is right; whether to offer it upstream as a PR.

Install on the phone: `scp -P 8022 android/augustus/build/outputs/apk/debug/augustus-debug.apk 100.87.32.87:storage/downloads/augustus-spen-debug.apk`, then over `ssh -p 8022 100.87.32.87` pipe it to `rish -c "pm install -S <size>"` (Shizuku must be up; otherwise the owner taps the APK in Downloads). Read pen logs with `rish -c "logcat -d -s AugustusPen:D"`.
