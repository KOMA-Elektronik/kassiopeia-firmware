#!/usr/bin/env bash
# Written in [Amber](https://amber-lang.com/)
# version: 0.4.0-alpha
# date: 2025-10-21 12:04:31
__0_LATEST_FW_MAJOR=1
__1_LATEST_FW_MINOR=0
__2_bin_dir="bin"
__3_bin_name="kassiopeia_fw"
__4_midi_default_path="bin/midi_default_values.bin"
# /////////////////////////////////////////////////////////////////////////////
# HELPERS
# /////////////////////////////////////////////////////////////////////////////
exit_program__0_v0() {
    echo "
--- Kassiopeia out ---"
    exit 1
}
read_input__1_v0() {
    __AMBER_VAL_0=$(read -r input; echo -n $input);
    __AS=$?;
    __AF_read_input1_v0="${__AMBER_VAL_0}";
    return 0
}
get_path_to_fw_binary__2_v0() {
    local dir=$1
    local name=$2
    local major=$3
    local minor=$4
    __AF_get_path_to_fw_binary2_v0="${dir}""/""${name}""_"${major}"_"${minor}".bin";
    return 0
}
# /////////////////////////////////////////////////////////////////////////////
# SUCCESS FUNCTIONS
# /////////////////////////////////////////////////////////////////////////////
success_connection__3_v0() {
    echo "##############################################################"
    echo "# Kassiopeia has been recognized and is ready to be flashed"'!'" #"
    echo "##############################################################
"
}
# /////////////////////////////////////////////////////////////////////////////
# CHECK FUNCTIONS
# /////////////////////////////////////////////////////////////////////////////
check_tooling__4_v0() {
    picotool version > /dev/null 2>&1;
    __AS=$?;
if [ $__AS != 0 ]; then
        echo "picotool is not found in PATH."
        echo "Please install via your preferred package manager or use 'nix-shell -p picotool' if you have nix installed"
        exit_program__0_v0 ;
        __AF_exit_program0_v0__43_9="$__AF_exit_program0_v0";
        echo "$__AF_exit_program0_v0__43_9" > /dev/null 2>&1
fi
}
check_connection__5_v0() {
    picotool info > /dev/null 2>&1;
    __AS=$?;
if [ $__AS != 0 ]; then
__AF_check_connection5_v0=''
return $__AS
fi
    success_connection__3_v0 ;
    __AF_success_connection3_v0__50_5="$__AF_success_connection3_v0";
    echo "$__AF_success_connection3_v0__50_5" > /dev/null 2>&1
}
# /////////////////////////////////////////////////////////////////////////////
# FLASHING FUNCTIONS
# /////////////////////////////////////////////////////////////////////////////
flash_firmware__6_v0() {
    local major_version=$1
    local minor_version=$2
    get_path_to_fw_binary__2_v0 "${__2_bin_dir}" "${__3_bin_name}" ${major_version} ${minor_version};
    __AF_get_path_to_fw_binary2_v0__58_24="${__AF_get_path_to_fw_binary2_v0}";
    picotool load -v ${__AF_get_path_to_fw_binary2_v0__58_24};
    __AS=$?;
if [ $__AS != 0 ]; then
__AF_flash_firmware6_v0=''
return $__AS
fi
}
flash_midi_default_config__7_v0() {
    # offset is determined in 'kassiopeia-code/system/inc/config.h' and keep in mind that the flash starts at 0x10000000
    picotool load -nuv -o 0x101FF000 ${__4_midi_default_path};
    __AS=$?;
if [ $__AS != 0 ]; then
__AF_flash_midi_default_config7_v0=''
return $__AS
fi
}
# /////////////////////////////////////////////////////////////////////////////
# BUSINESS LOGIC
# /////////////////////////////////////////////////////////////////////////////
fresh_install__8_v0() {
    echo "Make sure the Kassiopeia is connected in Bootloader mode via USB"'!'""
    while :
do
        echo "
Press 'Enter' to flash or 'e' to exit: "
        read_input__1_v0 ;
        __AF_read_input1_v0__76_12="${__AF_read_input1_v0}";
        if [ $([ "_${__AF_read_input1_v0__76_12}" != "_e" ]; echo $?) != 0 ]; then
            break
fi
        check_connection__5_v0 ;
        __AS=$?;
if [ $__AS != 0 ]; then
            echo "Can't find USB device."
            echo "Please open the Kassiopeia device (from the bottom) and press the 'Firmware Update' button while powering."
fi;
        __AF_check_connection5_v0__78_9="$__AF_check_connection5_v0";
        echo "$__AF_check_connection5_v0__78_9" > /dev/null 2>&1
        echo "Transfer latest firmware"'!'""
        flash_firmware__6_v0 ${__0_LATEST_FW_MAJOR} ${__1_LATEST_FW_MINOR};
        __AS=$?;
if [ $__AS != 0 ]; then
            echo "Couldn't upload firmware"
fi;
        __AF_flash_firmware6_v0__84_9="$__AF_flash_firmware6_v0";
        echo "$__AF_flash_firmware6_v0__84_9" > /dev/null 2>&1
        echo "Transfer MIDI default values"'!'""
        flash_midi_default_config__7_v0 ;
        __AS=$?;
if [ $__AS != 0 ]; then
            echo "Couldn't upload default MIDI values"
fi;
        __AF_flash_midi_default_config7_v0__89_9="$__AF_flash_midi_default_config7_v0";
        echo "$__AF_flash_midi_default_config7_v0__89_9" > /dev/null 2>&1
        echo "Transfer secret binary blob"'!'""
        echo "TBD"
        echo "I will now reboot the device..."
        picotool reboot;
        __AS=$?
done
}
# /////////////////////////////////////////////////////////////////////////////
# MAIN ENTRY
# /////////////////////////////////////////////////////////////////////////////

    echo "
--- Kassiopeia FW Flasher ---"
    check_tooling__4_v0 ;
    __AF_check_tooling4_v0__108_10="$__AF_check_tooling4_v0";
    echo "$__AF_check_tooling4_v0__108_10"
    mode=""
    while :
do
        echo "Is this the first time this device will be flashed (y/n)?"
        read_input__1_v0 ;
        __AF_read_input1_v0__114_22="${__AF_read_input1_v0}";
        choice="${__AF_read_input1_v0__114_22}"
        if [ $([ "_${choice}" != "_y" ]; echo $?) != 0 ]; then
            mode="fresh"
elif [ $([ "_${choice}" != "_yes" ]; echo $?) != 0 ]; then
            mode="fresh"
elif [ $([ "_${choice}" != "_Y" ]; echo $?) != 0 ]; then
            mode="fresh"
elif [ $([ "_${choice}" != "_YES" ]; echo $?) != 0 ]; then
            mode="fresh"
elif [ $([ "_${choice}" != "_n" ]; echo $?) != 0 ]; then
            mode="seasoned"
elif [ $([ "_${choice}" != "_N" ]; echo $?) != 0 ]; then
            mode="seasoned"
elif [ $([ "_${choice}" != "_no" ]; echo $?) != 0 ]; then
            mode="seasoned"
elif [ $([ "_${choice}" != "_No" ]; echo $?) != 0 ]; then
            mode="seasoned"
fi
        if [ $([ "_${mode}" != "_fresh" ]; echo $?) != 0 ]; then
            break
elif [ $([ "_${mode}" != "_seasoned" ]; echo $?) != 0 ]; then
            break
fi
done
    if [ $([ "_${mode}" != "_fresh" ]; echo $?) != 0 ]; then
        fresh_install__8_v0 ;
        __AF_fresh_install8_v0__137_26="$__AF_fresh_install8_v0";
        echo "$__AF_fresh_install8_v0__137_26" > /dev/null 2>&1
elif [ $([ "_${mode}" != "_seasoned" ]; echo $?) != 0 ]; then
        echo "empty, bye bye"
fi
    exit_program__0_v0 ;
    __AF_exit_program0_v0__141_5="$__AF_exit_program0_v0";
    echo "$__AF_exit_program0_v0__141_5" > /dev/null 2>&1
