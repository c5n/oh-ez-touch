#!/usr/bin/env bash

function print_help {
    echo "Usage:" >&2 ;
    echo "$0 -t target hostname1 hostname2 ..." >&2 ;
    echo "$0 -t target -l listfile" >&2 ;
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
