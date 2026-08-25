#!/usr/bin/env bash
# Written in [Amber](https://amber-lang.com/)
# version: 0.5.1-alpha
__LATEST_FW_MAJOR_0=1
__LATEST_FW_MINOR_1=1
bin_dir_2="bin"
bin_name_3="kassiopeia_fw"
midi_default_path_4="bin/midi_default_values.bin"
# /////////////////////////////////////////////////////////////////////////////
# HELPERS
# /////////////////////////////////////////////////////////////////////////////
exit_program__0_v0() {
    echo "
--- Kassiopeia out ---"
    exit 1
}

read_input__1_v0() {
    command_0="$(read -r input; echo -n $input)"
    __status=$?
    ret_read_input1_v0="${command_0}"
    return 0
}

get_path_to_fw_binary__2_v0() {
    local dir=$1
    local name=$2
    local major=$3
    local minor=$4
    ret_get_path_to_fw_binary2_v0="${dir}""/""${name}""_""${major}""_""${minor}"".bin"
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
    picotool version >/dev/null 2>&1
    __status=$?
    if [ "${__status}" != 0 ]; then
        echo "picotool is not found in PATH."
        echo "Please install via your preferred package manager or use 'nix-shell -p picotool' if you have nix installed"
        exit_program__0_v0 
    fi
}

check_connection__5_v0() {
    picotool info >/dev/null 2>&1
    __status=$?
    if [ "${__status}" != 0 ]; then
        ret_check_connection5_v0=''
        return "${__status}"
    fi
    success_connection__3_v0 
}

# /////////////////////////////////////////////////////////////////////////////
# FLASHING FUNCTIONS
# /////////////////////////////////////////////////////////////////////////////
flash_firmware__6_v0() {
    local major_version=$1
    local minor_version=$2
    get_path_to_fw_binary__2_v0 "${bin_dir_2}" "${bin_name_3}" "${major_version}" "${minor_version}"
    ret_get_path_to_fw_binary2_v0__58_24="${ret_get_path_to_fw_binary2_v0}"
    picotool load -v ${ret_get_path_to_fw_binary2_v0__58_24}
    __status=$?
    if [ "${__status}" != 0 ]; then
        ret_flash_firmware6_v0=''
        return "${__status}"
    fi
}

flash_midi_default_config__7_v0() {
    # offset is determined in 'kassiopeia-code/system/inc/config.h' and keep in mind that the flash starts at 0x10000000
    picotool load -nuv -o 0x101FF000 ${midi_default_path_4}
    __status=$?
    if [ "${__status}" != 0 ]; then
        ret_flash_midi_default_config7_v0=''
        return "${__status}"
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
        read_input__1_v0 
        ret_read_input1_v0__76_12="${ret_read_input1_v0}"
        if [ "$([ "_${ret_read_input1_v0__76_12}" != "_e" ]; echo $?)" != 0 ]; then
            break
        fi
        check_connection__5_v0 
        __status=$?
        if [ "${__status}" != 0 ]; then
            echo "Can't find USB device."
            echo "Please open the Kassiopeia device (from the bottom) and press the 'Firmware Update' button while powering."
        fi
        echo "Transfer latest firmware"'!'""
        flash_firmware__6_v0 "${__LATEST_FW_MAJOR_0}" "${__LATEST_FW_MINOR_1}"
        __status=$?
        if [ "${__status}" != 0 ]; then
            echo "Couldn't upload firmware"
        fi
        echo "Transfer MIDI default values"'!'""
        flash_midi_default_config__7_v0 
        __status=$?
        if [ "${__status}" != 0 ]; then
            echo "Couldn't upload default MIDI values"
        fi
        echo "Transfer secret binary blob"'!'""
        echo "TBD"
        echo "I will now reboot the device..."
        picotool reboot
        __status=$?
    done
}

# /////////////////////////////////////////////////////////////////////////////
# MAIN ENTRY
# /////////////////////////////////////////////////////////////////////////////
echo "
--- Kassiopeia FW Flasher ---"
check_tooling__4_v0 
echo ''
mode_5=""
while :
do
    echo "Is this the first time this device will be flashed (y/n)?"
    read_input__1_v0 
    choice_6="${ret_read_input1_v0}"
    if [ "$([ "_${choice_6}" != "_y" ]; echo $?)" != 0 ]; then
        mode_5="fresh"
    elif [ "$([ "_${choice_6}" != "_yes" ]; echo $?)" != 0 ]; then
        mode_5="fresh"
    elif [ "$([ "_${choice_6}" != "_Y" ]; echo $?)" != 0 ]; then
        mode_5="fresh"
    elif [ "$([ "_${choice_6}" != "_YES" ]; echo $?)" != 0 ]; then
        mode_5="fresh"
    elif [ "$([ "_${choice_6}" != "_n" ]; echo $?)" != 0 ]; then
        mode_5="seasoned"
    elif [ "$([ "_${choice_6}" != "_N" ]; echo $?)" != 0 ]; then
        mode_5="seasoned"
    elif [ "$([ "_${choice_6}" != "_no" ]; echo $?)" != 0 ]; then
        mode_5="seasoned"
    elif [ "$([ "_${choice_6}" != "_No" ]; echo $?)" != 0 ]; then
        mode_5="seasoned"
    fi
    if [ "$([ "_${mode_5}" != "_fresh" ]; echo $?)" != 0 ]; then
        break
    elif [ "$([ "_${mode_5}" != "_seasoned" ]; echo $?)" != 0 ]; then
        break
    fi
done
if [ "$([ "_${mode_5}" != "_fresh" ]; echo $?)" != 0 ]; then
    fresh_install__8_v0 
elif [ "$([ "_${mode_5}" != "_seasoned" ]; echo $?)" != 0 ]; then
    echo "empty, bye bye"
fi
exit_program__0_v0 
