# PlatformIO pre-build script: touch src/about.cpp so its build time (__DATE__/__TIME__) is always current.
Import("env")  # noqa: F821
import os

os.utime(os.path.join(env.subst("$PROJECT_DIR"), "src", "about.cpp"), None)  # noqa: F821
