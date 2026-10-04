"""Work around isolated LDF includes in Arduino-ESP32 3.x / PlatformIO."""
from pathlib import Path
Import("env")
if env.PioPlatform().name == "espressif32":
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    includes = [str(framework / "libraries" / name / "src") for name in ("Network", "WiFi")]
    env.AppendUnique(CPPPATH=includes)
    for library in env.GetLibBuilders():
        library.env.AppendUnique(CPPPATH=includes)
