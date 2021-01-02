#!/usr/bin/env bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

function print_help {
    cat <<EOF
Usage:
    $0 [-p] -t <target> <hostname1> <hostname2> ...
    $0 [-p] -t <target> -l <listfile>

    -p              Parallel multi process update

    -t <target>     Target should be one of the available build targets.
                    e.g. ArduiTouchMOD

    -l <listfile>   Text file with list of hostnames. One hostname per line.
EOF
}

function update_process() {
    echo "Updating ${2}..."
    curl --silent -F "name=@.pio/build/${1}/firmware.bin" http://${2}/update -o /dev/null
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed${NC} to update ${2}"
    else
        echo -e "${GREEN}Sucessfully${NC} updated ${2}"
    fi
}

OPT_FLAG_PARALLEL=0
unset OPT_TARGET
unset OPT_LISTFILE

while getopts ":t:l:p" opt; do
    case "$opt" in
        t) OPT_TARGET=$OPTARG ;;
        l) OPT_LISTFILE=$OPTARG ;;
        p) OPT_FLAG_PARALLEL=1 ;;
        ?) print_help; exit 2;;
    esac
done
shift $(( OPTIND - 1 ))

for HOSTNAME in "$@"; do
    update_process $OPT_TARGET $HOSTNAME &
    if [ "$OPT_FLAG_PARALLEL" -eq 0 ]; then
        # wait until child process is done
        wait
    fi
done

if [[ -f "$OPT_LISTFILE" ]]; then
    while IFS= read -r HOSTNAME
    do
        update_process $OPT_TARGET $HOSTNAME &
        if [ "$OPT_FLAG_PARALLEL" -eq 0 ]; then
            # wait until child process is done
            wait
        fi
    done <"$OPT_LISTFILE"
fi

# wait until all child processes are done
wait
