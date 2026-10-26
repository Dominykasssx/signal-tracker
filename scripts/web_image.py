# Post-build step for the web flasher. Next to firmware.bin in
# .pio/build/<env>/ it writes:
#   signal_finder-<env>.bin  image that flashes at offset 0
#   manifest.json            ESP Web Tools manifest for that image
#   board.json               board label, chip and servo pin for the web page
#
# ESP8266: firmware.bin already starts at offset 0.
# ESP32:   pioarduino merges bootloader, partition table and app into
#          firmware.factory.bin in its own post-build step, which runs first.
import json
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)


def chip_family(mcu):
    # "esp8266" -> "ESP8266", "esp32" -> "ESP32", "esp32s3" -> "ESP32-S3"
    mcu = mcu.upper()
    return mcu if mcu in ("ESP8266", "ESP32") else f"ESP32-{mcu[5:]}"


def write_web_image(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    pioenv = env["PIOENV"]
    mcu = env.BoardConfig().get("build.mcu")
    app = str(target[0])

    image = app if mcu == "esp8266" else env.subst("$BUILD_DIR/${PROGNAME}.factory.bin")
    # pioarduino only prints a message when its merge fails; don't ship a stale image.
    if not os.path.isfile(image) or os.path.getmtime(image) < os.path.getmtime(app):
        raise RuntimeError(f"{image} is missing or older than {app}")

    image_name = f"signal_finder-{pioenv}.bin"
    shutil.copyfile(image, os.path.join(build_dir, image_name))

    chip = chip_family(mcu)
    version = os.environ.get("FW_VERSION", "dev")
    manifest = {
        "name": "Signal Finder",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,  # the firmware doesn't speak Improv
        "builds": [{"chipFamily": chip, "parts": [{"path": image_name, "offset": 0}]}],
    }
    pin = env.GetProjectOption("custom_servo_pin")
    board = {
        "env": pioenv,
        "name": env.GetProjectOption("custom_board_name"),
        "chip": chip,
        "pin": env.GetProjectOption("custom_pin_label", f"GPIO{pin}"),
        "version": version,
    }
    for name, data in (("manifest.json", manifest), ("board.json", board)):
        with open(os.path.join(build_dir, name), "w") as f:
            json.dump(data, f, indent=2)

    print(f"Web flasher image: {image_name} ({chip}, {os.path.getsize(image)} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_web_image)  # noqa: F821
