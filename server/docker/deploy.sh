#!/bin/sh
# Uploads the built site (dist folder) to a server over FTPS or SFTP (FTP_PROTOCOL=ftps|sftp).
# Used by deploy_test.cmd and deploy_production.cmd — settings come from deploy_<target>.env.
#   deploy        — upload the site
#   deploy test   — only check the connection and list the target directory
#
# The site goes into its own directory (FTP_REMOTE_DIR, e.g. /web/hinasto). The new version is uploaded
# to <dir>.new first and swapped in at the end, so visitors never see a half-uploaded site;
# server-only items listed in KEEP (here: storage, which holds config.php written by the installer) are moved over from the previous version.
set -eu

: "${FTP_HOST:?FTP_HOST is missing in the .env file}"
: "${FTP_USER:?FTP_USER is missing in the .env file}"
: "${FTP_PASSWORD:?FTP_PASSWORD is missing in the .env file}"
: "${FTP_REMOTE_DIR:?FTP_REMOTE_DIR is missing in the .env file}"
FTP_IMPLICIT="${FTP_IMPLICIT:-no}"
FTP_PROTOCOL="${FTP_PROTOCOL:-ftps}"
# files/folders on the server that are not part of the site but must survive the swap (hosting config, certificates…);
# set per target in deploy_<target>.env (production: OVH files), empty by default
KEEP="${KEEP:-}"
# permission mode of storage/ and public/api/v1 after the swap (see the comment near the end of this file)
STORAGE_MODE="${STORAGE_MODE:-777}"
MODE="${1:-deploy}"

REMOTE="${FTP_REMOTE_DIR%/}"
PARENT="${REMOTE%/*}"; [ -n "$PARENT" ] || PARENT="/"
NAME="${REMOTE##*/}"
if [ -z "$NAME" ]; then
  echo "FTP_REMOTE_DIR must point to a directory of its own for the site (e.g. /web/hinasto), not to the root of the server." >&2
  exit 1
fi

if [ "$FTP_PROTOCOL" = "sftp" ]; then
  URL="sftp://$FTP_HOST:${FTP_PORT:-22}"   # SFTP (SSH)
elif [ "$FTP_IMPLICIT" = "yes" ]; then
  URL="ftps://$FTP_HOST:${FTP_PORT:-990}"  # implicit FTPS
else
  URL="ftp://$FTP_HOST:${FTP_PORT:-21}"    # explicit FTPS: AUTH TLS on the regular FTP port
fi

# the password reaches lftp through an environment variable, not the command line
export LFTP_PASSWORD="$FTP_PASSWORD"

SCRIPT=/tmp/deploy.lftp
connect() {
  if [ "$FTP_PROTOCOL" = "sftp" ]; then
    cat > "$SCRIPT" <<LFTP
# SFTP (SSH): the container is fresh every time, so the server's host key is accepted automatically
set sftp:auto-confirm yes
set net:timeout 20
set net:max-retries 2
set net:reconnect-interval-base 5
set cmd:fail-exit true
open --env-password -u "$FTP_USER" "$URL"
cd "$PARENT"
LFTP
  else
    cat > "$SCRIPT" <<LFTP
# encryption forced for login and data; the certificate (Let's Encrypt) is verified
set ftp:ssl-force true
set ftp:ssl-protect-data true
set ftp:ssl-protect-list true
set ssl:verify-certificate yes
set ftp:passive-mode true
set net:timeout 20
set net:max-retries 2
set net:reconnect-interval-base 5
set cmd:fail-exit true
open --env-password -u "$FTP_USER" "$URL"
cd "$PARENT"
LFTP
  fi
}

echo "Server: $URL  directory: $REMOTE  user: $FTP_USER"

if [ "$MODE" = "test" ]; then
  connect
  cat >> "$SCRIPT" <<LFTP
echo "Connection works. Contents of $PARENT:"
cls -1aF
LFTP
  lftp -f "$SCRIPT"
  exit 0
fi

if [ ! -f /dist/public/index.php ] || [ ! -d /dist/app ]; then
  echo "No prepared package in the dist folder (public/index.php, app/)." >&2
  exit 1
fi

# which KEEP items exist in the current version — only those are carried over (no errors for missing ones)
FOUND=""
if [ -n "$KEEP" ]; then
  connect
  echo "set cmd:fail-exit false" >> "$SCRIPT"
  echo "cls -1a \"$NAME/\"" >> "$SCRIPT"
  LISTING=$(lftp -f "$SCRIPT" 2>/dev/null | sed "s#/\$##; s#.*/##") || true
  for item in $KEEP; do
    if printf "%s\n" "$LISTING" | grep -qxF "$item"; then FOUND="$FOUND $item"; fi
  done
fi

# local file list: "<size> <path>" — the reference for checking the upload
(cd /dist && find . -type f -exec stat -c '%s %n' {} \; | sed 's# \./# #' | sort -k2) > /tmp/local.lst

# [1/3] upload into <dir>.new
upload() {
  connect
  cat >> "$SCRIPT" <<LFTP
# clean-up after a possibly interrupted deploy — errors (missing directory) are fine here
set cmd:fail-exit false
rm -rf "$NAME.new"
rm -rf "$NAME.old"
set cmd:fail-exit true
LFTP
  echo "mirror -R --parallel=2 --verbose=1 /dist \"$NAME.new\"" >> "$SCRIPT"
  lftp -f "$SCRIPT"
}

# compares sizes of the uploaded files with dist; prints the files that are missing or truncated
check() {
  connect
  echo "find -l \"$NAME.new\"" >> "$SCRIPT"
  lftp -f "$SCRIPT" | awk -v p="$NAME.new/" '$1 ~ /^-/ { path = $NF; sub("^" p, "", path); print $3, path }' | sort -k2 > /tmp/remote.lst
  # lines present locally but not (with the same size) on the server
  awk 'NR == FNR { r[$0] = 1; next } !($0 in r) { print $2 }' /tmp/remote.lst /tmp/local.lst > /tmp/bad.lst
  awk 'NR == FNR { r[$2] = $1; next } { print "  " $2 "  " (r[$2] == "" ? 0 : r[$2]) " / " $1 " B" }' /tmp/remote.lst /tmp/local.lst | grep -F -f /tmp/bad.lst || true
}

# FTPS on some servers (e.g. Synology) cuts the end off larger files; instead of starting over,
# continue each incomplete file from where it stopped (put -c = resume), so every pass adds the missing part
resume() {
  connect
  while read -r path; do
    echo "put -c \"/dist/$path\" -o \"$NAME.new/$path\"" >> "$SCRIPT"
  done < /tmp/bad.lst
  lftp -f "$SCRIPT"
}

echo "[1/3] Uploading the site to $NAME.new..."
upload
attempt=1
TRIES=10
while :; do
  echo "[2/3] Checking that every file arrived complete..."
  BAD=$(check)
  [ -z "$BAD" ] && break
  echo "Incomplete files on the server (present / expected):"
  echo "$BAD"
  if [ "$attempt" -ge "$TRIES" ]; then
    echo "Could not upload the site completely after $attempt attempts - the version on the server was NOT replaced." >&2
    exit 1
  fi
  attempt=$((attempt + 1))
  echo "Sending the missing parts (attempt $attempt of $TRIES)..."
  resume
done

echo "[3/3] Swapping in the new version..."
connect
cat >> "$SCRIPT" <<LFTP
set cmd:fail-exit false
mv "$NAME" "$NAME.old"
set cmd:fail-exit true
mv "$NAME.new" "$NAME"
set cmd:fail-exit false
LFTP
# carry over server-only files (hosting config etc.) from the previous version
for item in $FOUND; do
  echo "  keeping from the previous version: $item"
  echo "rm -rf \"$NAME/$item\"" >> "$SCRIPT"
  echo "mv \"$NAME.old/$item\" \"$NAME/$item\"" >> "$SCRIPT"
done
# PHP often runs as another user than the FTP login: storage/ (and public/api/v1, the static firmware copies) must be writable for it.
# STORAGE_MODE=777 is the safe default that works everywhere; on a host where PHP runs as the FTP user (or shares its group)
# set STORAGE_MODE=775 or 755 in deploy_<target>.env: a world-writable folder lets other accounts of a shared host change config.php.
echo "chmod $STORAGE_MODE \"$NAME/storage\"" >> "$SCRIPT"
echo "chmod $STORAGE_MODE \"$NAME/public/api/v1\"" >> "$SCRIPT"
echo "rm -rf \"$NAME.old\"" >> "$SCRIPT"
lftp -f "$SCRIPT"

echo "Done.${SITE_URL:+ Site: $SITE_URL}"
