#!/usr/bin/env bash

function print_help {
    cat <<EOF
Usage:
    $0 -t <target> <hostname1> <hostname2> ...
    $0 -t <target> -l <listfile>

    <target>    should be one of the available build targets.
                e.g. ArduiTouchMOD

    <listfile>  generic text file with list of hostnames. One hostname per line.
EOF
}

while getopts ":t:l:" opt; do
    case "$opt" in
        t) target=$OPTARG ;;
        l) listfile=$OPTARG ;;
        ?) print_help; exit 2;;
    esac
done
shift $(( OPTIND - 1 ))

for hostname in "$@"; do
    echo "Updating $hostname..."
    curl --progress-bar -F "name=@.pio/build/$target/firmware.bin" http://$hostname/update -o /dev/null
done

if [[ -f "$listfile" ]]; then
    while IFS= read -r hostname
    do
        echo "Updating $hostname..."
        curl --progress-bar -F "name=@.pio/build/$target/firmware.bin" http://$hostname/update -o /dev/null
    done <"$listfile"
fi
