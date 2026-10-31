# The Hinasto builder: everything needed to compile the firmware in every language and to make and use signing keys,
# with nothing installed on the host but Docker. Build it from the repository root:
#   docker build -f tools/builder/Dockerfile -t hinasto-builder .
# The ESP32 core is installed and the sketch is compiled once while the image is built, so that the first build of a user
# does not have to compile the core. Only what the ESP32-S2 needs is kept (the full core is several GB).
FROM python:3.12-slim-bookworm

ARG ARDUINO_CLI_VERSION=1.1.1
ARG ESP32_CORE=3.3.12

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates g++ make libmbedtls-dev \
 && rm -rf /var/lib/apt/lists/* \
 && pip install --no-cache-dir cryptography pyserial

ENV ARDUINO_DIRECTORIES_DATA=/opt/arduino15 \
    ARDUINO_DIRECTORIES_USER=/tmp/arduino-user \
    ARDUINO_DIRECTORIES_DOWNLOADS=/tmp/arduino-dl \
    ARDUINO_UPDATER_ENABLE_NOTIFICATION=false \
    HOME=/tmp

# One layer for install and clean-up (a later "rm" or "chmod -R" would not make the image smaller, it would only add a layer).
# The toolchain of the RISC-V chips, the libraries of the other chips and the debuggers are not needed for the S2.
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh -s ${ARDUINO_CLI_VERSION} \
 && arduino-cli config init --overwrite \
 && arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json \
 && arduino-cli core update-index \
 && for i in 1 2 3 4 5; do arduino-cli core install esp32:esp32@${ESP32_CORE} && break; echo "download failed, retry $i"; sleep 10; done \
 && arduino-cli core list | grep -q esp32:esp32 \
 && cd /opt/arduino15/packages/esp32/tools \
 && rm -rf esp-rv32 riscv32-esp-elf-gdb xtensa-esp-elf-gdb openocd-esp32 \
           esp32-libs esp32c3-libs esp32c5-libs esp32c6-libs esp32h2-libs esp32p4-libs esp32p4_es-libs esp32s3-libs \
 && rm -rf /tmp/arduino-dl \
 && mkdir -p /opt/build \
 && chmod 777 /opt/build /opt/arduino15 /opt/arduino15/tmp

# warm-up: compile the real sketch once (English); the objects stay in /opt/build and every build reuses them.
# umask 000: whoever runs the container (a user id given with -u) can overwrite them.
COPY sniffer /opt/warm/sniffer
RUN umask 000 \
 && echo '#define HN_LANG_EN' > /opt/warm/sniffer/lang_select.h \
 && arduino-cli compile --fqbn esp32:esp32:lolin_s2_mini:CDCOnBoot=default --build-path /opt/build /opt/warm/sniffer \
 && rm -rf /opt/warm

COPY tools/ota-sign.py tools/builder/hinasto /opt/hinasto/
RUN chmod 755 /opt/hinasto/hinasto

WORKDIR /work
ENTRYPOINT ["/opt/hinasto/hinasto"]
CMD ["help"]
