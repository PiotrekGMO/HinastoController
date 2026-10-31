<?php
// php tools/hash.php 'my password'   -> prints the value for 'admin_hash' in config.php
if ($argc < 2) {
    fwrite(STDERR, "usage: php tools/hash.php 'password'\n");
    exit(1);
}
echo password_hash($argv[1], PASSWORD_DEFAULT), "\n";
