#!/bin/bash -e

function print_headers(){

    if [[ -n "${1}" ]]; then
        label_len=${#1}
        span=$(((54 - $label_len) / 2))

        echo
        echo "= ======================================================== ="
        printf "%s %${span}s %s %${span}s %s\n" "=" "" "$1" "" "="
        echo "= ======================================================== ="
    else
        echo "= ========================= Done ========================= ="
        echo
    fi
}

function print_environment(){

    local compiler=gcc

    echo -n "Processors: "; grep -c ^processor /proc/cpuinfo
    grep ^MemTotal /proc/meminfo
    id; uname -a
    printenv
    echo '-----------------------------------------'
    cat /etc/*-release
    echo '-----------------------------------------'

    if [[ ! -z $CC ]]; then
        compiler=$CC
    fi
    echo 'Compiler version'
    $compiler --version
    echo '-----------------------------------------'
    $compiler -dM -E -x c /dev/null
    echo '-----------------------------------------'
}

function check_warnings(){

    cat compilation.log | grep "warning:" | awk '{total+=1}END{print "Total number of warnings: "total}'
}

# -----------  -----------
if [[ $1 == "environment" ]]; then
    print_headers 'Build environment '
    print_environment
    print_headers

elif [[ $1 == "git" ]]; then
    print_headers 'Commit'
    git log --pretty=format:"%h %cd %s" -1; echo
    print_headers

elif [[ $1 == "warnings" ]]; then
    print_headers 'Warning Report '
    check_warnings
    print_headers
fi
