#!/bin/bash -e
#
#  Copyright (C) 2004-2013 Savoir-Faire Linux Inc.
#
#  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#  Additional permission under GNU GPL version 3 section 7:
#
#  If you modify this program, or any covered work, by linking or
#  combining it with the OpenSSL project's OpenSSL library (or a
#  modified version of that library), containing parts covered by the
#  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
#  grants you additional permission to convey the resulting work.
#  Corresponding Source for a non-source form of such a combination
#  shall include the source code for the parts of OpenSSL used as well
#  as that of the covered work.
#

# Script used by Hudson continious integration server to build SFLphone

XML_RESULTS="cppunitresults.xml"
TEST=0
BUILD=
CODE_ANALYSIS=0
DOXYGEN=0

CONFIGDIR=~/.config
SFLCONFDIR=${CONFIGDIR}/sflphone

function run_code_analysis {
    # Check if cppcheck is installed on the system
    if [ `which cppcheck &>/dev/null ; echo $?` -ne 1 ] ; then
        pushd src
        cppcheck . --enable=all --xml --inline-suppr 2> cppcheck-report.xml
        popd
    fi
}


function gen_doxygen {
    # Check if doxygen is installed on the system
    if [ `which doxygen &>/dev/null ; echo $?` -ne 1 ] ; then
        pushd doc/doxygen
        doxygen core-doc.cfg.in
        popd
    fi
}

function launch_functional_test_daemon {
        # Run the python functional tests for the daemon

        # make sure no other instance are currently running
        killall sflphoned
        killall sipp

        # make sure the configuration directory created
        CONFDIR=~/.config
        SFLCONFDIR=${CONFDIR}/sflphone

        eval `dbus-launch --auto-syntax`

        if [ ! -d ${CONFDIR} ]; then
            mkdir ${CONFDIR}
        fi

        if [ ! -d ${SFLCONFDIR} ]; then
            mkdir ${SFLCONFDIR}
        fi

        # make sure the most recent version of the configuration
        # is installed
        pushd tools/pysflphone
            cp -f sflphoned.functest.yml ${SFLCONFDIR}
        popd

        # launch sflphone daemon, wait some time for
        # dbus registration to complete
        pushd daemon
            ./src/sflphoned &
            sleep 3
        popd

        # launch the test script
        pushd tools/pysflphone
            nosetests --with-xunit test_sflphone_dbus_interface.py
        popd
}


function build_daemon {
    pushd daemon
    # Run static analysis code tool
    if [ $CODE_ANALYSIS == 1 ]; then
        run_code_analysis
    fi
    # make distclean

    ./autogen.sh || exit 1
    # Compile pjproject first
    pushd libs
    ./compile_pjsip.sh
    popd

    # Compile the daemon
    ./configure --prefix=/usr
    make clean
    make -j
    # Remove the previous XML test file
    rm -rf $XML_RESULTS
    # Compile unit tests
    make check
    popd
}

function build_gnome {
    pushd daemon
    # Check if program is running
    if [ "$(pidof sflphoned)"]
    then
      killall sflphoned
    else
      echo "sflphoned not running"
    fi
    #make distclean

    # Compile pjproject first
    pushd libs
    ./compile_pjsip.sh
    popd

    # Compile daemon
    ./configure --prefix=/usr
    make clean
    make -j
    ./src/sflphoned &
    popd

    # Compile the plugins
    pushd plugins
    make distclean
    ./autogen.sh || exit 1
    ./configure --prefix=/usr
    make -j
    popd

    # Compile the client
    pushd gnome
    make distclean
    ./autogen.sh || exit 1
    ./configure --prefix=/usr
    make clean
    make -j 1
    make check
    popd
}

function build_kde {
   # Compile the KDE client
   pushd kde
   mkdir -p build
   cd build
   cmake ../
   make -j
   popd
}


if [ "$#" -eq 0 ]; then   # Script needs at least one command-line argument.
    echo "Usage $0 -b select which one to build: daemon or gnome
                  -t enable unit tests after build"
    exit $E_OPTERR
fi


git clean -f -d -x

while getopts ":b: t a d" opt; do
    case $opt in
        b)
            echo "-b was triggered. Parameter: $OPTARG" >&2
            BUILD=$OPTARG
            ;;
        t)
            echo "-t was triggered. Tests will be run" >&2
            TEST=1
            ;;
        a)
            echo "-a was triggered. Static code analysis will be run" >&2
            CODE_ANALYSIS=1
            ;;
        d)
            echo "-d was triggered. Doxygen documentation will be generated" >&2
            DOXYGEN=1
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            exit 1
            ;;
        :)
            echo "Option -$OPTARG requires an argument." >&2
            exit 1
            ;;
        esac
done

# Call appropriate build function, with parameters if needed
build_$BUILD

if [ $TEST == 1 ]; then
    launch_functional_test_daemon
fi

# SUCCESS
exit 0
