# Hinasto Controller server: syntax check + package + FTPS upload. Nothing needs to be installed on the PC except Docker.

# --- every PHP file must parse, otherwise nothing is deployed
FROM php:8.3-cli AS check
WORKDIR /src
COPY . .
RUN find . -name '*.php' -print0 | xargs -0 -n1 php -l > /dev/null \
 && date -u +'%Y-%m-%d %H:%M UTC' > BUILD \
 && rm -rf docker Dockerfile deploy_test.cmd deploy_production.cmd deploy_*.env.example .dockerignore .gitignore

# --- just the files to put on the server (document root on the server: <dir>/public):
#     docker build --target dist --output dist .
FROM scratch AS dist
COPY --from=check /src /

# --- FTPS/SFTP upload to a server (deploy_test.cmd)
FROM alpine:3.20 AS deploy
RUN apk add --no-cache lftp openssh-client ca-certificates
COPY docker/deploy.sh /usr/local/bin/deploy
# the script may have Windows line endings (CRLF) - sh would not run it
RUN sed -i 's/\r$//' /usr/local/bin/deploy && chmod +x /usr/local/bin/deploy
ENTRYPOINT ["deploy"]
