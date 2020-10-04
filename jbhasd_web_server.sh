#!/bin/bash

export TZ=Europe/Dublin

# determine path of this script
SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

JBHASD_WEB_SVR="jbhasd_web_server.py"
JBHASD_WEB_SVR_PATH="${SCRIPTPATH}/${JBHASD_WEB_SVR}"

# Logging
LOG_PURGE_DAYS=20
HOME_DIR=~
LOG_DIR="${HOME_DIR}/jbhasd_logs"
mkdir -p "${LOG_DIR}"
LOG_PREFIX="jbhasd_web_server"
LOG_FILE="${LOG_DIR}/${LOG_PREFIX}_`date +%Y%m%d%H%M%S`.log"

# Disable logging
# comment out to enable
LOG_FILE=/dev/null

# Check for jbhasd_web_server.py running
pgrep -f "${JBHASD_WEB_SVR}" >/dev/null
if [ $? -ne 0 ]
then
    nohup ${JBHASD_WEB_SVR_PATH} > ${LOG_FILE} 2>&1 &
fi

# Purge old logs
find ${LOG_DIR} -name ${LOG_PREFIX}* -mtime +${LOG_PURGE_DAYS} -exec rm \{} \; >/dev/null 2>&1

exit 0
