#!/bin/bash

# determine path of this script
SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

JBHASD_WEB_SVR="jbhasd_web_server.py"
JBHASD_WEB_SVR_PATH="${SCRIPTPATH}/${JBHASD_WEB_SVR}"

export TZ=Europe/Dublin

# Check for jbhasd_web_server.py running
pgrep -f "${JBHASD_WEB_SVR}" >/dev/null
if [ $? -ne 0 ]
then
    nohup ${JBHASD_WEB_SVR_PATH} > /dev/null 2>&1 &
    exit 0
fi

exit -1 
