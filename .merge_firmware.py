Import("env")
import os
import csv

APP_BIN = os.path.join("$BUILD_DIR", "${PROGNAME}.bin")
FS_BIN = os.path.join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}.bin")
MERGED_BIN = os.path.join("$BUILD_DIR", "${PROGNAME}_merged.bin")
BOARD_CONFIG = env.BoardConfig()
debug = False

def get_filesystem_offset_from_partition(env):
    """Extract filesystem offset from partition CSV file"""
    partition_file = BOARD_CONFIG.get("build.partitions", "partitions.csv")
    
    # Get the project root directory
    project_root = env.get("PROJECT_DIR")
    partition_path = os.path.join(project_root, partition_file)
    
    try:
        with open(partition_path, 'r') as f:
            reader = csv.DictReader(f, skipinitialspace=True)
            for row in reader:
                # Look for spiffs, littlefs, or data partition with spiffs subtype
                name = row.get('Name', '').strip()
                partition_type = row.get('Type', '').strip()
                subtype = row.get('SubType', '').strip()
                offset = row.get('Offset', '').strip()
                
                if (name in ['spiffs', 'littlefs'] or 
                    (partition_type == 'data' and subtype in ['spiffs', 'littlefs'])):
                    print(f"Found filesystem partition '{name}' at offset {offset}")
                    return offset
    except Exception as e:
        print(f"Error parsing partition file {partition_path}: {e}")
    
    # Fallback default
    print("Using default filesystem offset 0x2b0000")
    return "0x2b0000"

def merge_bin(source, target, env):
    # The list contains all extra images (bootloader, partitions, eboot) and
    # the final application binary
    # Get the filesystem offset from the partition table
    fs_offset = get_filesystem_offset_from_partition(env)
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])) + ["$ESP32_APP_OFFSET", APP_BIN] + [fs_offset, FS_BIN]

    # Run esptool to merge images into a single binary
    # Build the base command with merge_bin and flash configuration
    merge_cmd = [
        "$PYTHONEXE",
        "$OBJCOPY",
        "--chip",
        BOARD_CONFIG.get("build.mcu", "esp32"),
        "merge_bin",
        "--fill-flash-size",
        BOARD_CONFIG.get("upload.flash_size", "4MB"),
        "-o",
        MERGED_BIN,
    ]
    
    if debug:
        print("Attempting to build merged binary:", " ".join(merge_cmd + flash_images))
    else:
        env.Execute(" ".join(merge_cmd + flash_images))

def upload_merged_bin(source, target, env):
    flags=[
        f
        for f in env.get("UPLOADERFLAGS")
        if f not in env.Flatten(env.get("FLASH_EXTRA_IMAGES"))
    ]
    env.Execute(
        " ".join(["$PYTHONEXE","$UPLOADER", " ".join(flags), "0x0", MERGED_BIN])
    )

# Add a post action that runs esptoolpy to merge available flash images
#env.AddPostAction(APP_BIN , merge_bin)

env.AddCustomTarget("merge_bin", [None], [merge_bin])
env.AddCustomTarget("upload_merged_bin", ["merge_bin"], [upload_merged_bin])

# Patch the upload command to flash the merged binary at address 0x0
#env.Replace(
#    UPLOADERFLAGS=[
#            f
#            for f in env.get("UPLOADERFLAGS")
#            if f not in env.Flatten(env.get("FLASH_EXTRA_IMAGES"))
#        ]
#        + ["0x0", MERGED_BIN],
#    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
#)
