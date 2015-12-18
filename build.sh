#!/bin/bash

set -e

usemysql=0
full=0

while [ $# -gt 0 ]; do
    case $1 in
    --)
	shift
	break
	;;
    --mysql)
	usemysql=1
	export HAVE_MYSQL=YES
	;;
    -f|--full)
        full=1
        ;;
    -h|--help)
        echo "$0: build script"
        exit 0
        ;;
    *)
        echo "$0: bad usage, use -h for help"
        exit 1
    esac
    shift
done

dobuild () {
    export WORKSPACE=`pwd`
    rm -rf build
    rm -f ../root/$INSTALL_TAG/.opendnssec.*
    rm -f ../root/$INSTALL_TAG/.opendnssec-mysql.*
    rm -f ../root/$INSTALL_TAG/.daily-opendnssec.*
    rm -f ../root/$INSTALL_TAG/.daily-opendnssec-mysql.*
    if [ $usemysql -gt 0 ]; then
      ./testing/build-opendnssec-mysql.sh
    else
      ./testing/build-opendnssec.sh
    fi
}

doreport () {
    echo ""
    sed < junit.xml \
      -e '/<testsuite name="\([^"]*\)"/h' \
      -e '/<failure message="Failed"/{x;s/<testsuite name="\([^"]*\).*/\1/p}' \
      -e 'd'
}

export SVN_REVISION=1

if [ $(basename $(cd .. ; pwd)) = "label" ]
then
    pushd ../..
    export INSTALL_TAG=$(basename $(pwd))
    pushd ../SoftHSMv1
    export WORKSPACE=`pwd`
    rm -rf build ../../../root/$INSTALL_TAG/.softhsm.*
    bash ./testing/build-softhsm.sh
    popd
    popd
    export WORKSPACE=`pwd`
    rm -rf build ../../../root/$INSTALL_TAG/.opendnssec.*
    ./testing/build-opendnssec.sh
    cd testing
    export WORKSPACE=`pwd`
    ./test-opendnssec.sh
    cd ..
elif [ $(basename $(cd .. ; pwd)) = "workspace" ]
then
    export INSTALL_TAG=$(basename $(pwd))
    if [ $full -gt 0 ] ;then
      pushd ../SoftHSMv1
      export WORKSPACE=`pwd`
      rm -rf build ../root/$INSTALL_TAG/.softhsm.*
      bash ./testing/build-softhsm.sh
      popd
    fi
    dobuild
elif [ $(basename $(pwd)) = "testing" ]
then
    pushd ..
    export INSTALL_TAG=$(basename $(pwd))
    if [ $full -gt 0 ]; then
      dobuild
    fi
    popd
    export WORKSPACE=`pwd`
    ./test-opendnssec.sh
    doreport
elif [ $(basename $(pwd)) = "test-cases-daily.d" ]
then
    pushd ../..
    export INSTALL_TAG=$(basename $(pwd))
    if [ $full -gt 0 ]; then
      dobuild
    fi
    popd
    cd ..
    export WORKSPACE=`pwd`
    ./test-daily-opendnssec.sh
    doreport
elif [ $(basename $(pwd)) = "test-cases.d" ]
then
    pushd ../..
    export INSTALL_TAG=$(basename $(pwd))
    if [ $full -gt 0 ]; then
      dobuild
    fi
    popd
    cd ..
    export WORKSPACE=`pwd`
    ./test-opendnssec.sh
    doreport
elif [ \( $(basename $(cd .. ; pwd)) = "test-cases-daily.d" \) -o \
       \( $(basename $(cd .. ; pwd)) = "test-cases.d" \) ]
then
    pushd ../..
    pushd ..
    export INSTALL_TAG=$(basename $(pwd))
    if [ $full -gt 0 ]; then
      dobuild
    fi
    popd
    export WORKSPACE=`pwd`
    popd

    export WORKSPACE=`pwd`
    source ../../lib.sh && init || exit 1
    source ../../functions-opendnssec.sh || exit 1
    start_test opendnssec
    PRE_TEST=ods_pre_test
    POST_TEST=ods_post_test
    INTERRUPT_TEST=ods_interrupt_test
    test_ok=0
    (
        log_cleanup && syslog_cleanup
        ods_find_softhsm_module &&
        run_test ${PWD##*/} . $*
    ) && test_ok=1
    stop_test
    finish
    cd ..
else
    echo "Don't know where I am"
    exit 1
fi
